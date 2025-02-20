// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.hasItem;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import android.app.backup.BackupDataInput;
import android.app.backup.BackupDataOutput;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.ParcelFileDescriptor;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.stubbing.Answer;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

import org.chromium.base.BaseChromiumApplication;
import org.chromium.base.ContextUtils;
import org.chromium.base.PathUtils;
import org.chromium.base.library_loader.ProcessInitException;
import org.chromium.chrome.browser.firstrun.FirstRunSignInProcessor;
import org.chromium.chrome.browser.firstrun.FirstRunStatus;
import org.chromium.chrome.browser.init.AsyncInitTaskRunner;
import org.chromium.components.signin.ChromeSigninController;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.ObjectInputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;

/**
 * Unit tests for {@link org.chromium.chrome.browser.ChromeBackupAgent}.
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE, application = BaseChromiumApplication.class)
public class ChromeBackupAgentTest {
    private Context mContext;
    private ChromeBackupAgent mAgent;
    private AsyncInitTaskRunner mTaskRunner;

    private void setUpTestPrefs(SharedPreferences prefs) {
        SharedPreferences.Editor editor = prefs.edit();
        editor.putBoolean(FirstRunStatus.FIRST_RUN_FLOW_COMPLETE, true);
        editor.putBoolean(FirstRunSignInProcessor.FIRST_RUN_FLOW_SIGNIN_SETUP, false);
        editor.putString(ChromeSigninController.SIGNED_IN_ACCOUNT_KEY, "user1");
        editor.apply();
    }

    @Before
    public void setUp() {
        // Set up the context.
        mContext = RuntimeEnvironment.application.getApplicationContext();
        ContextUtils.initApplicationContextForTests(mContext);

        // Create the agent to test; override the native calls and fetching the task runner, and
        // spy on the agent to allow us to validate calls to these methods.
        mAgent = spy(new ChromeBackupAgent() {
            @Override
            AsyncInitTaskRunner createAsyncInitTaskRunner(CountDownLatch latch) {
                latch.countDown();
                return mTaskRunner;
            }

            @Override
            protected String[] nativeGetBoolBackupNames() {
                return new String[] {"pref1"};
            }

            @Override
            protected boolean[] nativeGetBoolBackupValues() {
                return new boolean[] {true};
            }

            @Override
            protected void nativeSetBoolBackupPrefs(String[] s, boolean[] b) {}
        });

        // Mock initializing the browser
        doReturn(true).when(mAgent).initializeBrowser(any(Context.class));

        // Mock the AsyncTaskRunner.
        mTaskRunner = mock(AsyncInitTaskRunner.class);
    }

    /**
     * Test method for {@link ChromeBackupAgent#onBackup} testing first backup
     *
     * @throws ProcessInitException
     */
    @Test
    @SuppressWarnings("unchecked")
    public void testOnBackup_firstBackup() throws FileNotFoundException, IOException,
                                                  ClassNotFoundException, ProcessInitException {
        // Mock the backup data.
        BackupDataOutput backupData = mock(BackupDataOutput.class);

        // Create a state file.
        File stateFile1 = File.createTempFile("Test", "");
        ParcelFileDescriptor newState =
                ParcelFileDescriptor.open(stateFile1, ParcelFileDescriptor.parseMode("w"));

        // Set up some preferences to back up.
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        setUpTestPrefs(prefs);

        // Run the test function.
        mAgent.onBackup(null, backupData, newState);

        // Check that the right things were written to the backup
        verify(backupData).writeEntityHeader("native.pref1", 1);
        verify(backupData)
                .writeEntityHeader("AndroidDefault." + FirstRunStatus.FIRST_RUN_FLOW_COMPLETE, 1);
        verify(backupData, times(2)).writeEntityData(new byte[] {1}, 1);
        verify(backupData)
                .writeEntityHeader(
                        "AndroidDefault." + FirstRunSignInProcessor.FIRST_RUN_FLOW_SIGNIN_SETUP, 1);
        verify(backupData).writeEntityData(new byte[] {0}, 1);
        byte[] unameBytes = "user1".getBytes();
        verify(backupData)
                .writeEntityHeader("AndroidDefault." + ChromeSigninController.SIGNED_IN_ACCOUNT_KEY,
                        unameBytes.length);
        verify(backupData).writeEntityData(unameBytes, unameBytes.length);

        newState.close();

        // Check that the state was saved correctly
        ObjectInputStream newStateStream = new ObjectInputStream(new FileInputStream(stateFile1));
        ArrayList<String> names = (ArrayList<String>) newStateStream.readObject();
        assertThat(names.size(), equalTo(4));
        assertThat(names, hasItem("native.pref1"));
        assertThat(names, hasItem("AndroidDefault." + FirstRunStatus.FIRST_RUN_FLOW_COMPLETE));
        assertThat(names,
                hasItem("AndroidDefault." + FirstRunSignInProcessor.FIRST_RUN_FLOW_SIGNIN_SETUP));
        assertThat(
                names, hasItem("AndroidDefault." + ChromeSigninController.SIGNED_IN_ACCOUNT_KEY));
        ArrayList<byte[]> values = (ArrayList<byte[]>) newStateStream.readObject();
        assertThat(values.size(), equalTo(4));
        assertThat(values, hasItem(unameBytes));
        assertThat(values, hasItem(new byte[] {0}));
        assertThat(values, hasItem(new byte[] {1}));
        // Make sure that there are no extra objects.
        assertThat(newStateStream.available(), equalTo(0));

        // Tidy up.
        newStateStream.close();
        stateFile1.delete();
    }

    /**
     * Test method for {@link ChromeBackupAgent#onBackup} a second backup with the same data
     */
    @Test
    @SuppressWarnings("unchecked")
    public void testOnBackup_duplicateBackup()
            throws FileNotFoundException, IOException, ClassNotFoundException {
        // Mock the backup data.
        BackupDataOutput backupData = mock(BackupDataOutput.class);

        // Create a state file.
        File stateFile1 = File.createTempFile("Test", "");
        ParcelFileDescriptor newState =
                ParcelFileDescriptor.open(stateFile1, ParcelFileDescriptor.parseMode("w"));

        // Set up some preferences to back up.
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        setUpTestPrefs(prefs);

        // Do a first backup.
        mAgent.onBackup(null, backupData, newState);

        // Minimal check on first backup, this isn't the test here.
        verify(backupData, times(4)).writeEntityHeader(anyString(), anyInt());
        verify(backupData, times(4)).writeEntityData(any(byte[].class), anyInt());

        newState.close();

        ParcelFileDescriptor oldState =
                ParcelFileDescriptor.open(stateFile1, ParcelFileDescriptor.parseMode("r"));
        File stateFile2 = File.createTempFile("Test", "");
        newState = ParcelFileDescriptor.open(stateFile2, ParcelFileDescriptor.parseMode("w"));

        // Try a second backup without changing any data
        mAgent.onBackup(oldState, backupData, newState);

        // Check that the second backup didn't write anything.
        verifyNoMoreInteractions(backupData);

        oldState.close();
        newState.close();

        // The two state files should contain identical data.
        ObjectInputStream oldStateStream = new ObjectInputStream(new FileInputStream(stateFile1));
        ArrayList<String> oldNames = (ArrayList<String>) oldStateStream.readObject();
        ArrayList<byte[]> oldValues = (ArrayList<byte[]>) oldStateStream.readObject();
        ObjectInputStream newStateStream = new ObjectInputStream(new FileInputStream(stateFile2));
        ArrayList<String> newNames = (ArrayList<String>) newStateStream.readObject();
        ArrayList<byte[]> newValues = (ArrayList<byte[]>) newStateStream.readObject();
        assertThat(newNames, equalTo(oldNames));
        assertTrue(Arrays.deepEquals(newValues.toArray(), oldValues.toArray()));
        assertThat(newStateStream.available(), equalTo(0));

        // Tidy up.
        oldStateStream.close();
        newStateStream.close();
        stateFile1.delete();
        stateFile2.delete();
    }

    /**
     * Test method for {@link ChromeBackupAgent#onBackup} a second backup with different data
     */
    @Test
    @SuppressWarnings("unchecked")
    public void testOnBackup_dataChanged()
            throws FileNotFoundException, IOException, ClassNotFoundException {
        // Mock the backup data.
        BackupDataOutput backupData = mock(BackupDataOutput.class);

        // Create a state file.
        File stateFile1 = File.createTempFile("Test", "");
        ParcelFileDescriptor newState =
                ParcelFileDescriptor.open(stateFile1, ParcelFileDescriptor.parseMode("w"));

        // Set up some preferences to back up.
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        setUpTestPrefs(prefs);

        // Do a first backup.
        mAgent.onBackup(null, backupData, newState);

        // Minimal check on first backup, this isn't the test here.
        verify(backupData, times(4)).writeEntityHeader(anyString(), anyInt());
        verify(backupData, times(4)).writeEntityData(any(byte[].class), anyInt());

        newState.close();

        ParcelFileDescriptor oldState =
                ParcelFileDescriptor.open(stateFile1, ParcelFileDescriptor.parseMode("r"));
        File stateFile2 = File.createTempFile("Test", "");
        newState = ParcelFileDescriptor.open(stateFile2, ParcelFileDescriptor.parseMode("w"));

        // Change some data.
        SharedPreferences.Editor editor = prefs.edit();
        editor.putBoolean(FirstRunSignInProcessor.FIRST_RUN_FLOW_SIGNIN_SETUP, true);
        editor.apply();

        // Do a second backup.
        mAgent.onBackup(oldState, backupData, newState);

        // Check that the second backup wrote something.
        verify(backupData, times(8)).writeEntityHeader(anyString(), anyInt());
        verify(backupData, times(8)).writeEntityData(any(byte[].class), anyInt());

        oldState.close();
        newState.close();

        // the two state files should contain different data (although the names are unchanged).
        ObjectInputStream oldStateStream = new ObjectInputStream(new FileInputStream(stateFile1));
        ArrayList<String> oldNames = (ArrayList<String>) oldStateStream.readObject();
        ArrayList<byte[]> oldValues = (ArrayList<byte[]>) oldStateStream.readObject();
        ObjectInputStream newStateStream = new ObjectInputStream(new FileInputStream(stateFile2));
        ArrayList<String> newNames = (ArrayList<String>) newStateStream.readObject();
        ArrayList<byte[]> newValues = (ArrayList<byte[]>) newStateStream.readObject();
        assertThat(newNames, equalTo(oldNames));
        assertFalse(Arrays.deepEquals(newValues.toArray(), oldValues.toArray()));
        assertThat(newStateStream.available(), equalTo(0));

        // Tidy up.
        oldStateStream.close();
        newStateStream.close();
        stateFile1.delete();
        stateFile2.delete();
    }

    private BackupDataInput createMockBackupData() throws IOException {
        // Mock the backup data
        BackupDataInput backupData = mock(BackupDataInput.class);

        final String[] keys = {"native.pref1", "native.pref2",
                "AndroidDefault." + FirstRunStatus.FIRST_RUN_FLOW_COMPLETE, "AndroidDefault.junk",
                "AndroidDefault." + ChromeSigninController.SIGNED_IN_ACCOUNT_KEY};
        byte[] unameBytes = "user1".getBytes();
        final byte[][] values = {{0}, {1}, {1}, {23, 42}, unameBytes};
        when(backupData.getKey()).thenAnswer(new Answer<String>() {
            private int mPos = 0;

            @Override
            public String answer(InvocationOnMock invocation) throws Throwable {
                return keys[mPos++];
            }
        });

        when(backupData.getDataSize()).thenAnswer(new Answer<Integer>() {
            private int mPos = 0;

            @Override
            public Integer answer(InvocationOnMock invocation) throws Throwable {
                return values[mPos++].length;
            }
        });

        when(backupData.readEntityData(any(byte[].class), anyInt(), anyInt()))
                .thenAnswer(new Answer<Integer>() {
                    private int mPos = 0;

                    @Override
                    public Integer answer(InvocationOnMock invocation) throws Throwable {
                        byte[] buffer = invocation.getArgument(0);
                        for (int i = 0; i < values[mPos].length; i++) {
                            buffer[i] = values[mPos][i];
                        }
                        return values[mPos++].length;
                    }
                });

        when(backupData.readNextHeader()).thenAnswer(new Answer<Boolean>() {
            private int mPos = 0;

            @Override
            public Boolean answer(InvocationOnMock invocation) throws Throwable {
                return mPos++ < 5;
            }
        });
        return backupData;
    }

    /**
     * Test method for {@link ChromeBackupAgent#onRestore}.
     *
     * @throws IOException
     * @throws ClassNotFoundException
     * @throws ProcessInitException
     * @throws InterruptedException
     */
    @Test
    public void testOnRestore_normal()
            throws IOException, ClassNotFoundException, ProcessInitException, InterruptedException {
        // Create a state file.
        File stateFile = File.createTempFile("Test", "");
        ParcelFileDescriptor newState =
                ParcelFileDescriptor.open(stateFile, ParcelFileDescriptor.parseMode("w"));

        BackupDataInput backupData = createMockBackupData();
        doReturn(true).when(mAgent).accountExistsOnDevice(any(String.class));

        // Do a restore.
        mAgent.onRestore(backupData, 0, newState);
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        assertTrue(prefs.getBoolean(FirstRunStatus.FIRST_RUN_FLOW_COMPLETE, false));
        assertFalse(prefs.contains("junk"));
        verify(mAgent).nativeSetBoolBackupPrefs(
                new String[] {"pref1", "pref2"}, new boolean[] {false, true});
        verify(mTaskRunner)
                .startBackgroundTasks(
                        false /* allocateChildConnection */, true /* initVariationSeed */);
        // The test mocks out everything that forces the AsyncTask used by PathUtils setup to
        // complete. If it isn't completed before the test exits Robolectric crashes with a null
        // pointer exception (although the test passes). Force it to complete by getting some data.
        PathUtils.getDataDirectory();
    }

    /**
     * Test method for {@link ChromeBackupAgent#onRestore} for a user that doesn't exist on the
     * device
     *
     * @throws IOException
     * @throws ClassNotFoundException
     * @throws ProcessInitException
     */
    @Test
    public void testOnRestore_badUser()
            throws IOException, ClassNotFoundException, ProcessInitException {
        // Create a state file.
        File stateFile = File.createTempFile("Test", "");
        ParcelFileDescriptor newState =
                ParcelFileDescriptor.open(stateFile, ParcelFileDescriptor.parseMode("w"));

        BackupDataInput backupData = createMockBackupData();
        doReturn(false).when(mAgent).accountExistsOnDevice(any(String.class));

        // Do a restore.
        mAgent.onRestore(backupData, 0, newState);
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        assertFalse(prefs.contains(FirstRunStatus.FIRST_RUN_FLOW_COMPLETE));
        verify(mAgent, never()).nativeSetBoolBackupPrefs(any(String[].class), any(boolean[].class));
        verify(mTaskRunner)
                .startBackgroundTasks(
                        false /* allocateChildConnection */, true /* initVariationSeed */);
        // The test mocks out everything that forces the AsyncTask used by PathUtils setup to
        // complete. If it isn't completed before the test exits Robolectric crashes with a null
        // pointer exception (although the test passes). Force it to complete by getting some data.
        PathUtils.getDataDirectory();
    }
}
