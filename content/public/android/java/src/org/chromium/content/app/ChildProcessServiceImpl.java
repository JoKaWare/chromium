// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.app;

import android.content.Context;
import android.content.Intent;
import android.graphics.SurfaceTexture;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.Parcelable;
import android.os.Process;
import android.os.RemoteException;
import android.view.Surface;

import org.chromium.base.BaseSwitches;
import org.chromium.base.CommandLine;
import org.chromium.base.ContextUtils;
import org.chromium.base.JNIUtils;
import org.chromium.base.Log;
import org.chromium.base.UnguessableToken;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.base.annotations.UsedByReflection;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.library_loader.Linker;
import org.chromium.base.library_loader.ProcessInitException;
import org.chromium.content.browser.ChildProcessConstants;
import org.chromium.content.browser.ChildProcessCreationParams;
import org.chromium.content.common.ContentSwitches;
import org.chromium.content.common.FileDescriptorInfo;
import org.chromium.content.common.IChildProcessCallback;
import org.chromium.content.common.IChildProcessService;
import org.chromium.content.common.SurfaceWrapper;

import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicReference;

/**
 * This class implements all of the functionality for {@link ChildProcessService} which owns an
 * object of {@link ChildProcessServiceImpl}.
 * It makes possible that WebAPK's ChildProcessService owns a ChildProcessServiceImpl object
 * and uses the same functionalities to create renderer process for WebAPKs when
 * "--enable-improved-a2hs" flag is turned on.
 */
@JNINamespace("content")
@SuppressWarnings("SynchronizeOnNonFinalField")
@MainDex
@UsedByReflection("WebApkSandboxedProcessService")
public class ChildProcessServiceImpl {
    private static final String MAIN_THREAD_NAME = "ChildProcessMain";
    private static final String TAG = "ChildProcessService";

    // Lock that protects the following members.
    private final Object mBinderLock = new Object();
    private IChildProcessCallback mCallback;
    // PID of the client of this service, set in bindToCaller().
    private int mBoundCallingPid;

    // This is the native "Main" thread for the renderer / utility process.
    private Thread mMainThread;
    // Parameters received via IPC, only accessed while holding the mMainThread monitor.
    private String[] mCommandLineParams;
    private int mCpuCount;
    private long mCpuFeatures;
    // File descriptors that should be registered natively.
    private FileDescriptorInfo[] mFdInfos;
    // Linker-specific parameters for this child process service.
    private ChromiumLinkerParams mLinkerParams;
    // Child library process type.
    private int mLibraryProcessType;

    private static AtomicReference<Context> sContext = new AtomicReference<>(null);
    private boolean mLibraryInitialized;

    /**
     * If >= 0 enables "validation of caller of {@link mBinder}'s methods". A RemoteException
     * is thrown when an application with a uid other than {@link mAuthorizedCallerUid} calls
     * {@link mBinder}'s methods.
     */
    private int mAuthorizedCallerUid;

    private final Semaphore mActivitySemaphore = new Semaphore(1);

    ChildProcessServiceImpl() {
        KillChildUncaughtExceptionHandler.maybeInstallHandler();
    }

    // Return a Linker instance. If testing, the Linker needs special setup.
    private Linker getLinker() {
        if (Linker.areTestsEnabled()) {
            // For testing, set the Linker implementation and the test runner
            // class name to match those used by the parent.
            assert mLinkerParams != null;
            Linker.setupForTesting(
                    mLinkerParams.mLinkerImplementationForTesting,
                    mLinkerParams.mTestRunnerClassNameForTesting);
        }
        return Linker.getInstance();
    }

    // Binder object used by clients for this service.
    private final IChildProcessService.Stub mBinder = new IChildProcessService.Stub() {
        // NOTE: Implement any IChildProcessService methods here.
        @Override
        public boolean bindToCaller() {
            synchronized (mBinderLock) {
                int callingPid = Binder.getCallingPid();
                if (mBoundCallingPid == 0) {
                    mBoundCallingPid = callingPid;
                } else if (mBoundCallingPid != callingPid) {
                    Log.e(TAG, "Service is already bound by pid %d, cannot bind for pid %d",
                            mBoundCallingPid, callingPid);
                    return false;
                }
            }
            return true;
        }

        @Override
        public int setupConnection(Bundle args, IChildProcessCallback callback) {
            int callingPid = Binder.getCallingPid();
            synchronized (mBinderLock) {
                if (mBoundCallingPid != callingPid) {
                    if (mBoundCallingPid == 0) {
                        Log.e(TAG, "Service has not been bound with bindToCaller()");
                    } else {
                        Log.e(TAG, "Client pid %d does not match the bound pid %d", callingPid,
                                mBoundCallingPid);
                    }
                    return -1;
                }

                mCallback = callback;
                getServiceInfo(args);
                return Process.myPid();
            }
        }

        @Override
        public void crashIntentionallyForTesting() {
            Process.killProcess(Process.myPid());
        }

        @Override
        public boolean onTransact(int arg0, Parcel arg1, Parcel arg2, int arg3)
                throws RemoteException {
            if (mAuthorizedCallerUid >= 0) {
                int callingUid = Binder.getCallingUid();
                if (callingUid != mAuthorizedCallerUid) {
                    throw new RemoteException("Unauthorized caller " + callingUid
                            + "does not match expected host=" + mAuthorizedCallerUid);
                }
            }
            return super.onTransact(arg0, arg1, arg2, arg3);
        }
    };

    // The ClassLoader for the host context.
    private ClassLoader mHostClassLoader;

    /* package */ static Context getContext() {
        return sContext.get();
    }

    /**
     * Loads Chrome's native libraries and initializes a ChildProcessServiceImpl.
     * @param context The application context.
     * @param hostContext The host context the library should be loaded with (i.e. Chrome).
     */
    @UsedByReflection("WebApkSandboxedProcessService")
    public void create(final Context context, final Context hostContext) {
        mHostClassLoader = hostContext.getClassLoader();
        Log.i(TAG, "Creating new ChildProcessService pid=%d", Process.myPid());
        if (sContext.get() != null) {
            throw new RuntimeException("Illegal child process reuse.");
        }
        sContext.set(context);

        // Initialize the context for the application that owns this ChildProcessServiceImpl object.
        ContextUtils.initApplicationContext(context);

        mMainThread = new Thread(new Runnable() {
            @Override
            @SuppressFBWarnings("DM_EXIT")
            public void run()  {
                try {
                    // CommandLine must be initialized before everything else.
                    synchronized (mMainThread) {
                        while (mCommandLineParams == null) {
                            mMainThread.wait();
                        }
                    }
                    CommandLine.init(mCommandLineParams);

                    if (ContentSwitches.SWITCH_RENDERER_PROCESS.equals(
                            CommandLine.getInstance().getSwitchValue(
                                    ContentSwitches.SWITCH_PROCESS_TYPE))) {
                        JNIUtils.enableSelectiveJniRegistration();
                    }

                    Linker linker = null;
                    boolean requestedSharedRelro = false;
                    if (Linker.isUsed()) {
                        assert mLinkerParams != null;
                        linker = getLinker();
                        if (mLinkerParams.mWaitForSharedRelro) {
                            requestedSharedRelro = true;
                            linker.initServiceProcess(mLinkerParams.mBaseLoadAddress);
                        } else {
                            linker.disableSharedRelros();
                        }
                    }
                    boolean isLoaded = false;
                    if (CommandLine.getInstance().hasSwitch(
                            BaseSwitches.RENDERER_WAIT_FOR_JAVA_DEBUGGER)) {
                        android.os.Debug.waitForDebugger();
                    }

                    boolean loadAtFixedAddressFailed = false;
                    try {
                        LibraryLoader.get(mLibraryProcessType)
                                .loadNowOverrideApplicationContext(hostContext);
                        isLoaded = true;
                    } catch (ProcessInitException e) {
                        if (requestedSharedRelro) {
                            Log.w(TAG, "Failed to load native library with shared RELRO, "
                                    + "retrying without");
                            loadAtFixedAddressFailed = true;
                        } else {
                            Log.e(TAG, "Failed to load native library", e);
                        }
                    }
                    if (!isLoaded && requestedSharedRelro) {
                        linker.disableSharedRelros();
                        try {
                            LibraryLoader.get(mLibraryProcessType)
                                    .loadNowOverrideApplicationContext(hostContext);
                            isLoaded = true;
                        } catch (ProcessInitException e) {
                            Log.e(TAG, "Failed to load native library on retry", e);
                        }
                    }
                    if (!isLoaded) {
                        System.exit(-1);
                    }
                    LibraryLoader.get(mLibraryProcessType)
                            .registerRendererProcessHistogram(requestedSharedRelro,
                                    loadAtFixedAddressFailed);
                    LibraryLoader.get(mLibraryProcessType).initialize();
                    synchronized (mMainThread) {
                        mLibraryInitialized = true;
                        mMainThread.notifyAll();
                        while (mFdInfos == null) {
                            mMainThread.wait();
                        }
                    }
                    for (FileDescriptorInfo fdInfo : mFdInfos) {
                        nativeRegisterGlobalFileDescriptor(
                                fdInfo.mId, fdInfo.mFd.detachFd(), fdInfo.mOffset, fdInfo.mSize);
                    }
                    nativeInitChildProcessImpl(ChildProcessServiceImpl.this, mCpuCount,
                            mCpuFeatures);
                    if (mActivitySemaphore.tryAcquire()) {
                        ContentMain.start();
                        nativeExitChildProcess();
                    }
                } catch (InterruptedException e) {
                    Log.w(TAG, "%s startup failed: %s", MAIN_THREAD_NAME, e);
                } catch (ProcessInitException e) {
                    Log.w(TAG, "%s startup failed: %s", MAIN_THREAD_NAME, e);
                }
            }
        }, MAIN_THREAD_NAME);
        mMainThread.start();
    }

    @SuppressFBWarnings("DM_EXIT")
    public void destroy() {
        Log.i(TAG, "Destroying ChildProcessService pid=%d", Process.myPid());
        if (mActivitySemaphore.tryAcquire()) {
            // TODO(crbug.com/457406): This is a bit hacky, but there is no known better solution
            // as this service will get reused (at least if not sandboxed).
            // In fact, we might really want to always exit() from onDestroy(), not just from
            // the early return here.
            System.exit(0);
            return;
        }
        synchronized (mMainThread) {
            try {
                while (!mLibraryInitialized) {
                    // Avoid a potential race in calling through to native code before the library
                    // has loaded.
                    mMainThread.wait();
                }
            } catch (InterruptedException e) {
                // Ignore
            }
        }
        // Try to shutdown the MainThread gracefully, but it might not
        // have chance to exit normally.
        nativeShutdownMainThread();
    }

    /*
     * Returns communication channel to service.
     * @param intent The intent that was used to bind to the service.
     * @param authorizedCallerUid If >= 0, enables "validation of service caller". A RemoteException
     *        is thrown when an application with a uid other than
     *        {@link authorizedCallerUid} calls the service's methods.
     */
    @UsedByReflection("WebApkSandboxedProcessService")
    public IBinder bind(Intent intent, int authorizedCallerUid) {
        mAuthorizedCallerUid = authorizedCallerUid;
        initializeParams(intent);
        return mBinder;
    }

    private void initializeParams(Intent intent) {
        synchronized (mMainThread) {
            // mLinkerParams is never used if Linker.isUsed() returns false.
            // See onCreate().
            mLinkerParams = new ChromiumLinkerParams(intent);
            mLibraryProcessType = ChildProcessCreationParams.getLibraryProcessType(intent);
            mMainThread.notifyAll();
        }
    }

    private void getServiceInfo(Bundle bundle) {
        // Required to unparcel FileDescriptorInfo.
        bundle.setClassLoader(mHostClassLoader);
        synchronized (mMainThread) {
            if (mCommandLineParams == null) {
                mCommandLineParams =
                        bundle.getStringArray(ChildProcessConstants.EXTRA_COMMAND_LINE);
                mMainThread.notifyAll();
            }
            // We must have received the command line by now
            assert mCommandLineParams != null;
            mCpuCount = bundle.getInt(ChildProcessConstants.EXTRA_CPU_COUNT);
            mCpuFeatures = bundle.getLong(ChildProcessConstants.EXTRA_CPU_FEATURES);
            assert mCpuCount > 0;
            Parcelable[] fdInfosAsParcelable =
                    bundle.getParcelableArray(ChildProcessConstants.EXTRA_FILES);
            if (fdInfosAsParcelable != null) {
                // For why this arraycopy is necessary:
                // http://stackoverflow.com/questions/8745893/i-dont-get-why-this-classcastexception-occurs
                mFdInfos = new FileDescriptorInfo[fdInfosAsParcelable.length];
                System.arraycopy(fdInfosAsParcelable, 0, mFdInfos, 0, fdInfosAsParcelable.length);
            }
            Bundle sharedRelros = bundle.getBundle(Linker.EXTRA_LINKER_SHARED_RELROS);
            if (sharedRelros != null) {
                getLinker().useSharedRelros(sharedRelros);
                sharedRelros = null;
            }
            mMainThread.notifyAll();
        }
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private void forwardSurfaceTextureForSurfaceRequest(
            UnguessableToken requestToken, SurfaceTexture surfaceTexture) {
        if (mCallback == null) {
            Log.e(TAG, "No callback interface has been provided.");
            return;
        }

        Surface surface = new Surface(surfaceTexture);

        try {
            mCallback.forwardSurfaceForSurfaceRequest(requestToken, surface);
        } catch (RemoteException e) {
            Log.e(TAG, "Unable to call forwardSurfaceForSurfaceRequest: %s", e);
            return;
        } finally {
            surface.release();
        }
    }

    @SuppressWarnings("unused")
    @CalledByNative
    private Surface getViewSurface(int surfaceId) {
        if (mCallback == null) {
            Log.e(TAG, "No callback interface has been provided.");
            return null;
        }

        try {
            SurfaceWrapper wrapper = mCallback.getViewSurface(surfaceId);
            return wrapper != null ? wrapper.getSurface() : null;
        } catch (RemoteException e) {
            Log.e(TAG, "Unable to call getViewSurface: %s", e);
            return null;
        }
    }

    /**
     * Helper for registering FileDescriptorInfo objects with GlobalFileDescriptors.
     * This includes the IPC channel, the crash dump signals and resource related
     * files.
     */
    private static native void nativeRegisterGlobalFileDescriptor(
            int id, int fd, long offset, long size);

    /**
     * The main entry point for a child process. This should be called from a new thread since
     * it will not return until the child process exits. See child_process_service.{h,cc}
     *
     * @param serviceImpl The current ChildProcessServiceImpl object.
     * renderer.
     */
    private static native void nativeInitChildProcessImpl(
            ChildProcessServiceImpl serviceImpl, int cpuCount, long cpuFeatures);

    /**
     * Force the child process to exit.
     */
    private static native void nativeExitChildProcess();

    private native void nativeShutdownMainThread();
}
