// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.offlinepages;

import android.app.NotificationManager;
import android.content.Context;
import android.os.Environment;
import android.text.TextUtils;
import android.util.LongSparseArray;

import org.chromium.base.Callback;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.base.test.util.Manual;
import org.chromium.base.test.util.TimeoutScale;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.browser.offlinepages.evaluation.OfflinePageEvaluationBridge;
import org.chromium.chrome.browser.offlinepages.evaluation.OfflinePageEvaluationBridge.OfflinePageEvaluationObserver;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.components.offlinepages.BackgroundSavePageResult;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;

import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;

/**
 * Tests OfflinePageBridge.SavePageLater over a batch of urls.
 * Tests against a list of top EM urls, try to call SavePageLater on each of the url. It also
 * record metrics (failure rate, time elapsed etc.) by writing metrics to a file on external
 * storage. This will always use prerenderer.
 */
@CommandLineFlags.Add({"disable-features=BackgroundLoader"})
public class OfflinePageSavePageLaterEvaluationTest
        extends ChromeActivityTestCaseBase<ChromeActivity> {
    /**
     * Class which is used to calculate time difference.
     */
    static class TimeDelta {
        public void setStartTime(Long startTime) {
            mStartTime = startTime;
        }
        public void setEndTime(Long endTime) {
            mEndTime = endTime;
        }
        // Return time delta in milliseconds.
        public Long getTimeDelta() {
            return mEndTime - mStartTime;
        }

        private Long mStartTime, mEndTime;
    }

    static class RequestMetadata {
        public long mId;
        public OfflinePageItem mPage;
        public int mStatus;
        public TimeDelta mTimeDelta;
        public String mUrl;
    }

    private static final String TAG = "OPSPLEvaluation";
    private static final String TAG_PROGRESS = "EvalProgress@@@@@@";
    private static final String NAMESPACE = "async_loading";
    private static final String NEW_LINE = System.getProperty("line.separator");
    private static final String DELIMITER = ";";
    private static final String CONFIG_FILE_PATH = "paquete/test_config";
    private static final String SAVED_PAGES_EXTERNAL_PATH = "paquete/archives";
    private static final String INPUT_FILE_PATH = "paquete/offline_eval_urls.txt";
    private static final String LOG_OUTPUT_FILE_PATH = "paquete/offline_eval_logs.txt";
    private static final String RESULT_OUTPUT_FILE_PATH = "paquete/offline_eval_results.txt";
    private static final int GET_PAGES_TIMEOUT_MS = 30000;
    private static final int PAGE_MODEL_LOAD_TIMEOUT_MS = 30000;
    private static final int REMOVE_REQUESTS_TIMEOUT_MS = 30000;

    private OfflinePageEvaluationBridge mBridge;
    private OfflinePageEvaluationObserver mObserver;

    private CountDownLatch mCompletionLatch;
    private List<String> mUrls;
    private int mCount;
    private boolean mIsUserRequested;
    private boolean mUseTestScheduler;
    private int mScheduleBatchSize;
    private boolean mUseBackgroundLoader;

    private LongSparseArray<RequestMetadata> mRequestMetadata;

    public OfflinePageSavePageLaterEvaluationTest() {
        super(ChromeActivity.class);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mRequestMetadata = new LongSparseArray<RequestMetadata>();
        mCount = 0;
    }

    @Override
    protected void tearDown() throws Exception {
        NotificationManager notificationManager =
                (NotificationManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.NOTIFICATION_SERVICE);
        notificationManager.cancelAll();
        final Semaphore mClearingSemaphore = new Semaphore(0);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                assert mBridge != null;
                mBridge.getRequestsInQueue(new Callback<SavePageRequest[]>() {
                    @Override
                    public void onResult(SavePageRequest[] results) {
                        ArrayList<Long> ids = new ArrayList<Long>(results.length);
                        for (int i = 0; i < results.length; i++) {
                            ids.add(results[i].getRequestId());
                        }
                        mBridge.removeRequestsFromQueue(ids, new Callback<Integer>() {
                            @Override
                            public void onResult(Integer removedCount) {
                                mClearingSemaphore.release();
                            }
                        });
                    }
                });
            }
        });
        checkTrue(mClearingSemaphore.tryAcquire(REMOVE_REQUESTS_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "Timed out when clearing remaining requests!");
        mBridge.closeLog();
        super.tearDown();
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityOnBlankPage();
    }

    /**
     * Get a reader for a given input file path.
     */
    private BufferedReader getInputStream(String inputFilePath) throws FileNotFoundException {
        FileReader fileReader =
                new FileReader(new File(Environment.getExternalStorageDirectory(), inputFilePath));
        BufferedReader bufferedReader = new BufferedReader(fileReader);
        return bufferedReader;
    }

    /**
     * Get a writer for given output file path.
     */
    private OutputStreamWriter getOutputStream(String outputFilePath) throws IOException {
        File outputFile = new File(Environment.getExternalStorageDirectory(), outputFilePath);
        return new FileWriter(outputFile);
    }

    /**
     * Get the directory on external storage for storing saved pages.
     */
    private File getExternalArchiveDir() {
        File externalArchiveDir =
                new File(Environment.getExternalStorageDirectory(), SAVED_PAGES_EXTERNAL_PATH);
        try {
            // Clear the old archive folder.
            if (externalArchiveDir.exists()) {
                String[] files = externalArchiveDir.list();
                if (files != null) {
                    for (String file : files) {
                        File currentFile = new File(externalArchiveDir.getPath(), file);
                        if (!currentFile.delete()) {
                            log(TAG, file + " cannot be deleted when clearing previous archives.");
                        }
                    }
                }
            } else if (!externalArchiveDir.mkdir()) {
                log(TAG, "Cannot create directory on external storage to store saved pages.");
            }
        } catch (SecurityException e) {
            log(TAG, "Failed to delete or create external archive folder!");
        }
        return externalArchiveDir;
    }

    /**
     * Print log message in output file through evaluation bridge.
     */
    private void log(String tag, String format, Object... args) {
        mBridge.log(tag, String.format(format, args));
    }

    /**
     * Assert the condition is true, otherwise abort the test and log.
     */
    private void checkTrue(boolean condition, String message) {
        if (!condition) {
            log(TAG, message);
            fail();
        }
    }

    /**
     * Initializes the evaluation bridge which will be used.
     * @param useCustomScheduler True if customized scheduler (the one with immediate scheduling)
     *                           will be used. False otherwise.
     * @param useBackgroundLoader True if use background loader. False if use prerenderer.
     */
    private void initializeBridgeForProfile(final boolean useTestingScheduler,
            final boolean useBackgroundLoader) throws InterruptedException {
        final Semaphore semaphore = new Semaphore(0);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Profile profile = Profile.getLastUsedProfile();
                mBridge = OfflinePageEvaluationBridge.getForProfile(
                        profile, useTestingScheduler, useBackgroundLoader);
                if (mBridge == null) {
                    fail("OfflinePageEvaluationBridge initialization failed!");
                    return;
                }
                if (mBridge.isOfflinePageModelLoaded()) {
                    semaphore.release();
                    return;
                }
                mBridge.addObserver(new OfflinePageEvaluationObserver() {
                    @Override
                    public void offlinePageModelLoaded() {
                        semaphore.release();
                        mBridge.removeObserver(this);
                    }
                });
            }
        });
        checkTrue(semaphore.tryAcquire(PAGE_MODEL_LOAD_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "Timed out when loading OfflinePageModel!");
    }

    /**
     * Set up the input/output, bridge and observer we're going to use.
     * @param useCustomScheduler True if customized scheduler (the one with immediate scheduling)
     *                           will be used. False otherwise.
     * @param useBackgroundLoader True if use background loader. False if use prerenderer.
     */
    protected void setUpIOAndBridge(final boolean useCustomScheduler,
            final boolean useBackgroundLoader) throws InterruptedException {
        try {
            getUrlListFromInputFile(INPUT_FILE_PATH);
        } catch (IOException e) {
            Log.wtf(TAG, "Cannot read input file!", e);
        }
        checkTrue(mUrls != null, "URLs weren't loaded.");
        checkTrue(mUrls.size() > 0, "No valid URLs in the input file.");

        if (mScheduleBatchSize == 0) {
            mScheduleBatchSize = mUrls.size();
        }

        initializeBridgeForProfile(useCustomScheduler, useBackgroundLoader);
        mObserver = new OfflinePageEvaluationObserver() {
            public void savePageRequestAdded(SavePageRequest request) {
                RequestMetadata metadata = new RequestMetadata();
                metadata.mId = request.getRequestId();
                metadata.mUrl = request.getUrl();
                metadata.mStatus = -1;
                TimeDelta timeDelta = new TimeDelta();
                timeDelta.setStartTime(System.currentTimeMillis());
                metadata.mTimeDelta = timeDelta;
                mRequestMetadata.put(request.getRequestId(), metadata);
                log(TAG, "SavePageRequest Added for %s with id %d.", metadata.mUrl, metadata.mId);
            }
            public void savePageRequestCompleted(SavePageRequest request, int status) {
                RequestMetadata metadata = mRequestMetadata.get(request.getRequestId());
                metadata.mTimeDelta.setEndTime(System.currentTimeMillis());
                if (metadata.mStatus == -1) {
                    mCount++;
                    log(TAG_PROGRESS, "%s is saved with result: %s. (%d/%d)", metadata.mUrl,
                            statusToString(status), mCount, mUrls.size());
                } else {
                    log(TAG, "The request for url: " + metadata.mUrl
                                    + " has more than one completion callbacks!");
                    log(TAG, "Previous status: " + metadata.mStatus + ". Current: " + status);
                }
                metadata.mStatus = status;
                if (mCount == mUrls.size() || mCount % mScheduleBatchSize == 0) {
                    mCompletionLatch.countDown();
                    return;
                }
            }
            public void savePageRequestChanged(SavePageRequest request) {}
        };
        mBridge.addObserver(mObserver);
        try {
            File logOutputFile =
                    new File(Environment.getExternalStorageDirectory(), LOG_OUTPUT_FILE_PATH);
            mBridge.setLogOutputFile(logOutputFile);
        } catch (IOException e) {
            Log.wtf(TAG, "Cannot set log output file!", e);
        }
    }

    /**
     * Calls SavePageLater on the bridge to try to offline an url.
     * @param url The url to be saved.
     * @param namespace The namespace this request belongs to.
     */
    private void savePageLater(final String url, final String namespace)
            throws InterruptedException {
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mBridge.savePageLater(url, namespace, mIsUserRequested);
            }
        });
    }

    private void processUrls(List<String> urls) throws InterruptedException, IOException {
        if (mBridge == null) {
            fail("Test initialization error, aborting. No results would be written.");
            return;
        }
        int count = 0;
        log(TAG_PROGRESS, "# of Urls in file: " + mUrls.size());
        for (int i = 0; i < mUrls.size(); i++) {
            savePageLater(mUrls.get(i), NAMESPACE);
            count++;
            if (count == mScheduleBatchSize || i == mUrls.size() - 1) {
                count = 0;
                mCompletionLatch = new CountDownLatch(1);
                mCompletionLatch.await();
            }
        }
        writeResults();
        log(TAG_PROGRESS, "Urls processing DONE.");
    }

    private void getUrlListFromInputFile(String inputFilePath)
            throws IOException, InterruptedException {
        mUrls = new ArrayList<String>();
        try {
            BufferedReader bufferedReader = getInputStream(inputFilePath);
            try {
                String url;
                while ((url = bufferedReader.readLine()) != null) {
                    if (!TextUtils.isEmpty(url)) {
                        mUrls.add(url);
                    }
                }
            } finally {
                if (bufferedReader != null) {
                    bufferedReader.close();
                }
            }
        } catch (FileNotFoundException e) {
            Log.e(TAG, e.getMessage(), e);
            fail(String.format("URL file %s is not found.", inputFilePath));
        }
    }

    // Translate the int value of status to BackgroundSavePageResult.
    private String statusToString(int status) {
        switch (status) {
            case BackgroundSavePageResult.SUCCESS:
                return "SUCCESS";
            case BackgroundSavePageResult.LOADING_FAILURE:
                return "LOADING_FAILURE";
            case BackgroundSavePageResult.LOADING_CANCELED:
                return "LOADING_CANCELED";
            case BackgroundSavePageResult.FOREGROUND_CANCELED:
                return "FOREGROUND_CANCELED";
            case BackgroundSavePageResult.SAVE_FAILED:
                return "SAVE_FAILED";
            case BackgroundSavePageResult.EXPIRED:
                return "EXPIRED";
            case BackgroundSavePageResult.RETRY_COUNT_EXCEEDED:
                return "RETRY_COUNT_EXCEEDED";
            case BackgroundSavePageResult.START_COUNT_EXCEEDED:
                return "START_COUNT_EXCEEDED";
            case BackgroundSavePageResult.REMOVED:
                return "REMOVED";
            case -1:
                return "NOT_COMPLETED";
            default:
                return "UNDEFINED_STATUS";
        }
    }

    /**
     * Get saved offline pages and align them with the metadata we got from testing.
     */
    private void loadSavedPages() throws InterruptedException {
        final Semaphore semaphore = new Semaphore(0);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mBridge.getAllPages(new Callback<List<OfflinePageItem>>() {
                    @Override
                    public void onResult(List<OfflinePageItem> pages) {
                        for (OfflinePageItem page : pages) {
                            mRequestMetadata.get(page.getOfflineId()).mPage = page;
                        }
                        semaphore.release();
                    }
                });
            }
        });
        checkTrue(semaphore.tryAcquire(GET_PAGES_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "Timed out when getting all offline pages");
    }

    /**
     * Writes test results to output file. The format would be:
     * URL;OFFLINE_STATUS;FILE_SIZE;TIME_SINCE_TEST_START
     * If page loading failed, size and timestamp would not be written to file.
     * Examples:
     * http://indianrail.gov.in/;START_COUNT_EXCEEDED
     * http://www.21cineplex.com/;SUCCESS;1160 KB;171700
     * https://www.google.com/;SUCCESS;110 KB;273805
     * At the end of the file there will be a summary:
     * Total requested URLs: XX, Completed: XX, Failed: XX, Failure Rate: XX.XX%
     */
    private void writeResults() throws IOException, InterruptedException {
        loadSavedPages();
        OutputStreamWriter output = getOutputStream(RESULT_OUTPUT_FILE_PATH);
        try {
            int failedCount = 0;
            if (mCount < mUrls.size()) {
                log(TAG, "Test terminated before all requests completed.");
            }
            File externalArchiveDir = getExternalArchiveDir();
            for (int i = 0; i < mRequestMetadata.size(); i++) {
                RequestMetadata metadata = mRequestMetadata.valueAt(i);
                long requestId = metadata.mId;
                int status = metadata.mStatus;
                String url = metadata.mUrl;
                OfflinePageItem page = metadata.mPage;
                if (page == null) {
                    output.write(url + DELIMITER + statusToString(status) + NEW_LINE);
                    if (status != -1) {
                        failedCount++;
                    }
                    continue;
                }
                output.write(metadata.mUrl + DELIMITER + statusToString(status) + DELIMITER
                        + page.getFileSize() / 1000 + " KB" + DELIMITER
                        + metadata.mTimeDelta.getTimeDelta() + NEW_LINE);
                // Move the page to external storage if external archive exists.
                File originalPage = new File(page.getFilePath());
                File externalPage = new File(externalArchiveDir, originalPage.getName());
                if (!OfflinePageUtils.copyToShareableLocation(originalPage, externalPage)) {
                    log(TAG, "Saved page for url " + page.getUrl() + " cannot be moved.");
                }
            }
            output.write(String.format(
                    "Total requested URLs: %d, Completed: %d, Failed: %d, Failure Rate: %.2f%%"
                            + NEW_LINE,
                    mUrls.size(), mCount, failedCount, (failedCount * 100.0 / mCount)));
        } catch (FileNotFoundException e) {
            Log.e(TAG, e.getMessage(), e);
        } finally {
            if (output != null) {
                output.close();
            }
        }
    }

    /**
     * Method to parse config files for test parameters.
     */
    public void parseConfigFile() throws IOException {
        Properties properties = new Properties();
        InputStream inputStream = null;
        try {
            File configFile = new File(Environment.getExternalStorageDirectory(), CONFIG_FILE_PATH);
            inputStream = new FileInputStream(configFile);
            properties.load(inputStream);
            mIsUserRequested = Boolean.parseBoolean(properties.getProperty("IsUserRequested"));
            mUseTestScheduler = Boolean.parseBoolean(properties.getProperty("UseTestScheduler"));
            mScheduleBatchSize = Integer.parseInt(properties.getProperty("ScheduleBatchSize"));
            mUseBackgroundLoader =
                    Boolean.parseBoolean(properties.getProperty("UseBackgroundLoader"));
        } catch (FileNotFoundException e) {
            Log.e(TAG, e.getMessage(), e);
            fail(String.format(
                    "Config file %s is not found, aborting the test.", CONFIG_FILE_PATH));
        } catch (NumberFormatException e) {
            Log.e(TAG, e.getMessage(), e);
            fail("Error parsing config file, aborting test.");
        } finally {
            if (inputStream != null) {
                inputStream.close();
            }
        }
    }

    /**
     * The test is the entry point for all kinds of testing of SavePageLater.
     * It is encouraged to use run_offline_page_evaluation_test.py to run this test.
     * TimeoutScale is set to 4, in case we hit the hard limit for @Manual tests(10 hours)
     * and gets killed. It expand the timeout to 10 * 4 hours.
     * We won't be treating svelte devices differently so enable the feature which would let
     * immediate processing also works on svelte devices. This flag will *not* affect normal
     * devices.
     */
    @Manual
    @TimeoutScale(4)
    @CommandLineFlags
            .Add({"enable-features=OfflinePagesSvelteConcurrentLoading"})
    @CommandLineFlags.Remove({"disable-features=OfflinePagesSvelteConcurrentLoading"})
    public void testFailureRate() throws IOException, InterruptedException {
        parseConfigFile();
        setUpIOAndBridge(mUseTestScheduler, mUseBackgroundLoader);
        processUrls(mUrls);
    }

    /**
     * Runs testFailureRate with background loader enabled.
     * We won't be treating svelte devices differently so enable the feature which would let
     * immediate processing also works on svelte devices. This flag will *not* affect normal
     * devices.
     */
    @Manual
    @TimeoutScale(4)
    @CommandLineFlags
            .Add({"enable-features=BackgroundLoader,OfflinePagesSvelteConcurrentLoading"})
    @CommandLineFlags
            .Remove({"disable-features=BackgroundLoader,OfflinePagesSvelteConcurrentLoading"})
    public void testBackgroundLoaderFailureRate() throws IOException, InterruptedException {
        testFailureRate();
    }
}
