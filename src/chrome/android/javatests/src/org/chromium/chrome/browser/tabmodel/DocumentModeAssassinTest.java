// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.tabmodel;

import android.content.Context;
import android.content.SharedPreferences;
import android.test.MoreAsserts;
import android.test.suitebuilder.annotation.MediumTest;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.StreamUtil;
import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.AdvancedMockContext;
import org.chromium.chrome.browser.TabState;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.DocumentModeAssassin.DocumentModeAssassinForTesting;
import org.chromium.chrome.browser.tabmodel.DocumentModeAssassin.DocumentModeAssassinObserver;
import org.chromium.chrome.browser.tabmodel.TabPersistentStoreTest.MockTabPersistentStoreObserver;
import org.chromium.chrome.browser.tabmodel.TabPersistentStoreTest.TestTabModelSelector;
import org.chromium.chrome.browser.tabmodel.TestTabModelDirectory.TabStateInfo;
import org.chromium.chrome.browser.tabmodel.document.ActivityDelegate;
import org.chromium.chrome.browser.tabmodel.document.DocumentTabModel;
import org.chromium.chrome.browser.tabmodel.document.DocumentTabModelImpl;
import org.chromium.chrome.browser.tabmodel.document.MockDocumentTabModel;
import org.chromium.chrome.test.util.browser.tabmodel.document.MockActivityDelegate;
import org.chromium.content.browser.test.NativeLibraryTestBase;
import org.chromium.content.browser.test.util.CallbackHelper;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

import javax.annotation.Nullable;

/**
 * Tests permanent migration from document mode to tabbed mode.
 *
 * This test is meant to run without the native library loaded, only loading it when confirming
 * that files have been written correctly.
 */
public class DocumentModeAssassinTest extends NativeLibraryTestBase {
    private static final String TAG = "DocumentModeAssassin";

    private static final String DOCUMENT_MODE_DIRECTORY_NAME = "ChromeDocumentActivity";
    private static final String TABBED_MODE_DIRECTORY_NAME = "app_tabs";
    private static final int TABBED_MODE_INDEX = 0;

    private static final TestTabModelDirectory.TabModelMetaDataInfo TEST_INFO =
            TestTabModelDirectory.TAB_MODEL_METADATA_V5_NO_M18;
    private static final TestTabModelDirectory.TabStateInfo[] TAB_STATE_INFO = TEST_INFO.contents;

    private AdvancedMockContext mContext;
    private TestTabModelDirectory mDocumentModeDirectory;
    private TestTabModelDirectory mTabbedModeDirectory;
    private ActivityDelegate mActivityDelegate;
    private CallbackHelper mFinishAllDocumentActivitiesCallback;
    private MockDocumentTabModel mTestTabModel;

    @Override
    public void setUp() throws Exception {
        super.setUp();
        mContext = new AdvancedMockContext(getInstrumentation().getTargetContext());
        mDocumentModeDirectory = new TestTabModelDirectory(
                mContext, DOCUMENT_MODE_DIRECTORY_NAME, null);
        mTabbedModeDirectory = new TestTabModelDirectory(
                mContext, TABBED_MODE_DIRECTORY_NAME, String.valueOf(TABBED_MODE_INDEX));

        // Only checks that a particular function was called.
        mActivityDelegate = new MockActivityDelegate() {
            @Override
            public void finishAllDocumentActivities() {
                super.finishAllDocumentActivities();
                mFinishAllDocumentActivitiesCallback.notifyCalled();
            }
        };
        mFinishAllDocumentActivitiesCallback = new CallbackHelper();

        // Make a DocumentTabModel that serves up tabs from our fake list.
        mTestTabModel = new MockDocumentTabModel(false) {
            @Override
            public int getCount() {
                return TAB_STATE_INFO.length;
            }

            @Override
            public Tab getTabAt(int index) {
                return new Tab(TAB_STATE_INFO[index].tabId, false, null);
            }

            @Override
            public int index() {
                // Figure out which tab actually has the correct ID.
                for (int i = 0; i < TEST_INFO.contents.length; i++) {
                    if (TEST_INFO.selectedTabId == TEST_INFO.contents[i].tabId) return i;
                }
                fail();
                return TabModel.INVALID_TAB_INDEX;
            }

            @Override
            public String getInitialUrlForDocument(int tabId) {
                for (int i = 0; i < TAB_STATE_INFO.length; i++) {
                    if (TAB_STATE_INFO[i].tabId == tabId) {
                        return TAB_STATE_INFO[i].url;
                    }
                }
                fail();
                return null;
            }
        };
    }

    @Override
    public void tearDown() throws Exception {
        try {
            if (mDocumentModeDirectory != null) mDocumentModeDirectory.tearDown();
        } catch (Exception e) {
            Log.e(TAG, "Failed to clean up document mode directory.");
        }

        try {
            if (mTabbedModeDirectory != null) mTabbedModeDirectory.tearDown();
        } catch (Exception e) {
            Log.e(TAG, "Failed to clean up tabbed mode directory.");
        }

        super.tearDown();
    }

    private void writeUselessFileToDirectory(File directory, String filename) {
        File file = new File(directory, filename);
        FileOutputStream outputStream = null;
        try {
            outputStream = new FileOutputStream(file);
            outputStream.write(116);
        } catch (IOException e) {
            assert false : "Failed to create " + filename;
        } finally {
            StreamUtil.closeQuietly(outputStream);
        }
    }

    /** Tests that migration finishes immediately if migration isn't necessary. */
    @SmallTest
    public void testMigrationSkippedIfUnneeded() throws Exception {
        final CallbackHelper doneCallback = new CallbackHelper();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_DONE) {
                    doneCallback.notifyCalled();
                } else {
                    fail("Unexpected stage: " + newStage);
                }
            }
        };

        final DocumentModeAssassin assassin =
                createAssassinForTesting(DocumentModeAssassin.STAGE_UNINITIALIZED, true, false);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, doneCallback.getCallCount());
                assassin.migrateFromDocumentToTabbedMode();
            }
        });

        doneCallback.waitForCallback(0);
    }

    /** Tests the full pathway. */
    @MediumTest
    public void testFullMigration() throws Exception {
        final CallbackHelper copyStartedCallback = new CallbackHelper();
        final CallbackHelper copyCallback = new CallbackHelper();
        final CallbackHelper copyDoneCallback = new CallbackHelper();
        final CallbackHelper writeStartedCallback = new CallbackHelper();
        final CallbackHelper writeDoneCallback = new CallbackHelper();
        final CallbackHelper changeStartedCallback = new CallbackHelper();
        final CallbackHelper changeDoneCallback = new CallbackHelper();
        final CallbackHelper deletionStartedCallback = new CallbackHelper();
        final CallbackHelper deletionDoneCallback = new CallbackHelper();
        final ArrayList<Integer> copiedIds = new ArrayList<Integer>();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_STARTED) {
                    copyStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_DONE) {
                    copyDoneCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_STARTED) {
                    writeStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_DONE) {
                    writeDoneCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_STARTED) {
                    changeStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_DONE) {
                    changeDoneCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_DELETION_STARTED) {
                    deletionStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_DONE) {
                    deletionDoneCallback.notifyCalled();
                }
            }

            @Override
            public void onTabStateFileCopied(int copiedId) {
                copiedIds.add(copiedId);
                copyCallback.notifyCalled();
            }
        };

        setUpDocumentDirectory();
        final DocumentModeAssassin assassin = createAssassinForTesting(
                DocumentModeAssassin.STAGE_UNINITIALIZED, true, true);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, copyStartedCallback.getCallCount());
                assertEquals(0, copyDoneCallback.getCallCount());
                assertEquals(0, writeStartedCallback.getCallCount());
                assertEquals(0, writeDoneCallback.getCallCount());
                assertEquals(0, changeStartedCallback.getCallCount());
                assertEquals(0, changeDoneCallback.getCallCount());
                assertEquals(0, deletionStartedCallback.getCallCount());
                assertEquals(0, deletionDoneCallback.getCallCount());
                assassin.migrateFromDocumentToTabbedMode();
            }
        });

        // Confirm that files got copied over.
        copyStartedCallback.waitForCallback(0);
        copyDoneCallback.waitForCallback(0);
        confirmCopySuccessful(copyCallback, copiedIds);

        // Confirm that the TabModelMetadata file was copied over.
        writeStartedCallback.waitForCallback(0);
        writeDoneCallback.waitForCallback(0);
        File[] tabbedModeFilesAfter = mTabbedModeDirectory.getDataDirectory().listFiles();
        assertNotNull(tabbedModeFilesAfter);
        assertEquals(copiedIds.size() + 1, tabbedModeFilesAfter.length);

        // Confirm that the user got moved into tabbed mode.
        changeStartedCallback.waitForCallback(0);
        mFinishAllDocumentActivitiesCallback.waitForCallback(0);
        changeDoneCallback.waitForCallback(0);
        confirmUserIsInTabbedMode();

        // Confirm that the document mode directory got nuked.
        deletionStartedCallback.waitForCallback(0);
        deletionDoneCallback.waitForCallback(0);
        assertFalse(mDocumentModeDirectory.getDataDirectory().exists());

        // Confirm that the TabModelMetadata file is correct.  This is done after the pipeline
        // completes to avoid loading the native library.
        confirmTabModelMetadataFileIsCorrect(null);
    }

    /** Tests the fallback pathway, triggered when the user has failed to migrate too many times. */
    @MediumTest
    public void testForceMigrationAfterFailures() throws Exception {
        final CallbackHelper writeDoneCallback = new CallbackHelper();
        final CallbackHelper changeStartedCallback = new CallbackHelper();
        final CallbackHelper changeDoneCallback = new CallbackHelper();
        final CallbackHelper deletionStartedCallback = new CallbackHelper();
        final CallbackHelper deletionDoneCallback = new CallbackHelper();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_STARTED
                        || newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_DONE
                        || newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_STARTED) {
                    fail();
                } else if (newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_DONE) {
                    writeDoneCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_STARTED) {
                    changeStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_DONE) {
                    changeDoneCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_DELETION_STARTED) {
                    deletionStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_DONE) {
                    deletionDoneCallback.notifyCalled();
                }
            }
        };

        // Indicate that migration has already failed multiple times.
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        SharedPreferences.Editor editor = prefs.edit();
        editor.putInt(DocumentModeAssassin.PREF_NUM_MIGRATION_ATTEMPTS,
                DocumentModeAssassin.MAX_MIGRATION_ATTEMPTS_BEFORE_FAILURE);
        editor.apply();

        setUpDocumentDirectory();
        final DocumentModeAssassin assassin =
                createAssassinForTesting(DocumentModeAssassin.STAGE_UNINITIALIZED, true, true);
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, writeDoneCallback.getCallCount());
                assertEquals(0, changeStartedCallback.getCallCount());
                assertEquals(0, changeDoneCallback.getCallCount());
                assertEquals(0, deletionStartedCallback.getCallCount());
                assertEquals(0, deletionDoneCallback.getCallCount());
                assassin.migrateFromDocumentToTabbedMode();
            }
        });

        // Confirm that the user got moved into tabbed mode.
        writeDoneCallback.waitForCallback(0);
        changeStartedCallback.waitForCallback(0);
        mFinishAllDocumentActivitiesCallback.waitForCallback(0);
        changeDoneCallback.waitForCallback(0);
        confirmUserIsInTabbedMode();

        // Confirm that the document mode directory got nuked.
        deletionStartedCallback.waitForCallback(0);
        deletionDoneCallback.waitForCallback(0);
        assertFalse(mDocumentModeDirectory.getDataDirectory().exists());
    }

    /**
     * Tests that the preference to knock a user out of document mode is properly set.
     */
    @SmallTest
    public void testForceToTabbedMode() throws Exception {
        final CallbackHelper changeStartedCallback = new CallbackHelper();
        final CallbackHelper changeDoneCallback = new CallbackHelper();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_STARTED) {
                    changeStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_CHANGE_SETTINGS_DONE) {
                    changeDoneCallback.notifyCalled();
                }
            }
        };
        final DocumentModeAssassin assassin = createAssassinForTesting(
                DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_DONE, false, true);

        // Write out the preference and wait for it.
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, changeStartedCallback.getCallCount());
                assertEquals(0, changeDoneCallback.getCallCount());
                assassin.switchToTabbedMode();
            }
        });
        changeStartedCallback.waitForCallback(0);
        mFinishAllDocumentActivitiesCallback.waitForCallback(0);
        changeDoneCallback.waitForCallback(0);
        confirmUserIsInTabbedMode();
    }

    private void confirmUserIsInTabbedMode() {
        // Check that the preference was written out correctly.
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                assertTrue(DocumentModeAssassin.isOptedOutOfDocumentMode());
            }
        });
    }

    /**
     * Tests that the {@link DocumentTabModel}'s data is properly saved out for a
     * {@link TabModelImpl}.
     */
    @MediumTest
    public void testWriteTabModelMetadata() throws Exception {
        // Write the TabState files into the tabbed mode directory directly, but fail to copy just
        // one of them.  This forces the TabPersistentStore to improvise and use the initial URL
        // that we provide.
        final TabStateInfo failureCase = TestTabModelDirectory.V2_HAARETZ;
        final Set<Integer> migratedTabIds = new HashSet<Integer>();
        for (int i = 0; i < TAB_STATE_INFO.length; i++) {
            if (failureCase.tabId == TAB_STATE_INFO[i].tabId) continue;
            migratedTabIds.add(TAB_STATE_INFO[i].tabId);
            mTabbedModeDirectory.writeTabStateFile(TAB_STATE_INFO[i]);
        }

        // Start writing the data out.
        final CallbackHelper writeStartedCallback = new CallbackHelper();
        final CallbackHelper writeDoneCallback = new CallbackHelper();
        final DocumentModeAssassin assassin = createAssassinForTesting(
                DocumentModeAssassin.STAGE_COPY_TAB_STATES_DONE, false, true);
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_STARTED) {
                    writeStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_WRITE_TABMODEL_METADATA_DONE) {
                    writeDoneCallback.notifyCalled();
                }
            }
        };

        File[] tabbedModeFilesBefore = mTabbedModeDirectory.getDataDirectory().listFiles();
        assertNotNull(tabbedModeFilesBefore);
        int numFilesBefore = tabbedModeFilesBefore.length;
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, writeStartedCallback.getCallCount());
                assertEquals(0, writeDoneCallback.getCallCount());
                assassin.writeTabModelMetadata(migratedTabIds);
            }
        });

        // Wait and confirm that the tabbed mode metadata file was written out.
        writeStartedCallback.waitForCallback(0);
        writeDoneCallback.waitForCallback(0);
        File[] tabbedModeFilesAfter = mTabbedModeDirectory.getDataDirectory().listFiles();
        assertNotNull(tabbedModeFilesAfter);
        assertEquals(numFilesBefore + 1, tabbedModeFilesAfter.length);
        confirmTabModelMetadataFileIsCorrect(failureCase);
    }

    private void confirmTabModelMetadataFileIsCorrect(@Nullable TabStateInfo failureCase)
            throws Exception {
        // Load up the metadata file via a TabPersistentStore to make sure that it contains all of
        // the migrated tab information.
        loadNativeLibraryAndInitBrowserProcess();
        TabPersistentStore.setBaseStateDirectoryForTests(mTabbedModeDirectory.getBaseDirectory());

        TestTabModelSelector selector = new TestTabModelSelector(mContext);
        TabPersistentStore store = selector.mTabPersistentStore;
        MockTabPersistentStoreObserver mockObserver = selector.mTabPersistentStoreObserver;

        // Load up the TabModel metadata.
        int numExpectedTabs = TEST_INFO.numRegularTabs + TEST_INFO.numIncognitoTabs;
        store.loadState(false /* ignoreIncognitoFiles */);
        mockObserver.initializedCallback.waitForCallback(0, 1);
        assertEquals(numExpectedTabs, mockObserver.mTabCountAtStartup);
        mockObserver.detailsReadCallback.waitForCallback(0, TEST_INFO.contents.length);
        assertEquals(numExpectedTabs, mockObserver.details.size());

        // Restore the TabStates, then confirm that things were restored correctly, in the right tab
        // order and with the right URLs.
        store.restoreTabs(true);
        mockObserver.stateLoadedCallback.waitForCallback(0, 1);
        assertEquals(TEST_INFO.numRegularTabs, selector.getModel(false).getCount());
        assertEquals(TEST_INFO.numIncognitoTabs, selector.getModel(true).getCount());

        for (int i = 0; i < numExpectedTabs; i++) {
            int savedTabId = TEST_INFO.contents[i].tabId;
            int restoredId = selector.getModel(false).getTabAt(i).getId();

            if (failureCase != null && failureCase.tabId == savedTabId) {
                // The Tab without a written TabState file will get a new Tab ID.
                MoreAsserts.assertNotEqual(failureCase.tabId, restoredId);
            } else {
                // Restored Tabs get the ID that they expected.
                assertEquals(savedTabId, restoredId);
            }

            // The URL wasn't written into the metadata file, so this will be correct only if
            // the TabPersistentStore successfully read the TabState files we planted earlier.
            // In the case where we intentionally didn't copy the TabState file, the metadata file
            // will contain the URL that the DocumentActivity was initially launched for, which gets
            // used as the fallback.
            assertEquals(TEST_INFO.contents[i].url, selector.getModel(false).getTabAt(i).getUrl());
        }
    }

    /** Checks that all TabState files are copied successfully. */
    @MediumTest
    public void testCopyTabStateFiles() throws Exception {
        performCopyTest(Tab.INVALID_TAB_ID);
    }

    /** Confirms that the selected tab's TabState file is copied before all the other ones. */
    @MediumTest
    public void testSelectedTabCopiedFirst() throws Exception {
        performCopyTest(TestTabModelDirectory.V2_HAARETZ.tabId);
    }

    private void performCopyTest(final int selectedTabId) throws Exception {
        final CallbackHelper copyStartedCallback = new CallbackHelper();
        final CallbackHelper copyDoneCallback = new CallbackHelper();
        final CallbackHelper copyCallback = new CallbackHelper();
        final AtomicInteger firstCopiedId = new AtomicInteger(Tab.INVALID_TAB_ID);
        final ArrayList<Integer> copiedIds = new ArrayList<Integer>();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_STARTED) {
                    copyStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_COPY_TAB_STATES_DONE) {
                    copyDoneCallback.notifyCalled();
                }
            }

            @Override
            public void onTabStateFileCopied(int copiedId) {
                if (firstCopiedId.get() == Tab.INVALID_TAB_ID) firstCopiedId.set(copiedId);
                copiedIds.add(copiedId);
                copyCallback.notifyCalled();
            }
        };

        // Kick off copying the tab states.
        setUpDocumentDirectory();
        final DocumentModeAssassin assassin =
                createAssassinForTesting(DocumentModeAssassin.STAGE_INITIALIZED, false, true);
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, copyStartedCallback.getCallCount());
                assertEquals(0, copyDoneCallback.getCallCount());
                assertEquals(0, copyCallback.getCallCount());
                assassin.copyTabStateFiles(selectedTabId);
            }
        });
        copyStartedCallback.waitForCallback(0);

        // Confirm that the first TabState file copied back is the selected one.
        copyCallback.waitForCallback(0);
        if (selectedTabId != Tab.INVALID_TAB_ID) assertEquals(selectedTabId, firstCopiedId.get());

        copyDoneCallback.waitForCallback(0);
        confirmCopySuccessful(copyCallback, copiedIds);
    }

    private void confirmCopySuccessful(CallbackHelper copyCallback, ArrayList<Integer> copiedIds) {
        // Confirm that all the TabState files were copied over.
        assertEquals(TAB_STATE_INFO.length, copyCallback.getCallCount());
        assertEquals(TAB_STATE_INFO.length, copiedIds.size());
        for (int i = 0; i < TAB_STATE_INFO.length; i++) {
            assertTrue(copiedIds.contains(TAB_STATE_INFO[i].tabId));
        }

        // Confirm that the legitimate TabState files were all copied over.
        File[] tabbedModeFilesAfter = mTabbedModeDirectory.getDataDirectory().listFiles();
        assertNotNull(tabbedModeFilesAfter);
        assertEquals(TAB_STATE_INFO.length, tabbedModeFilesAfter.length);

        for (int i = 0; i < TAB_STATE_INFO.length; i++) {
            boolean found = false;
            for (int j = 0; j < tabbedModeFilesAfter.length && !found; j++) {
                found |= TAB_STATE_INFO[i].filename.equals(tabbedModeFilesAfter[j].getName());
            }
            assertTrue("Couldn't find file: " + TAB_STATE_INFO[i].filename, found);
        }
    }

    /** Confirms that document mode data gets completely deleted. */
    @MediumTest
    public void testDeleteDocumentData() throws Exception {
        final CallbackHelper deleteStartedCallback = new CallbackHelper();
        final CallbackHelper deleteDoneCallback = new CallbackHelper();
        final DocumentModeAssassinObserver observer = new DocumentModeAssassinObserver() {
            @Override
            public void onStageChange(int newStage) {
                if (newStage == DocumentModeAssassin.STAGE_DELETION_STARTED) {
                    deleteStartedCallback.notifyCalled();
                } else if (newStage == DocumentModeAssassin.STAGE_DONE) {
                    deleteDoneCallback.notifyCalled();
                }
            }

            @Override
            public void onTabStateFileCopied(int copiedId) {
                fail();
            }
        };

        // Plant a random shared preference that should be deleted.
        final String keyToBeDeleted = "to_be_deleted";
        SharedPreferences prefs = mContext.getSharedPreferences(
                DocumentTabModelImpl.PREF_PACKAGE, Context.MODE_PRIVATE);
        SharedPreferences.Editor editor = prefs.edit();
        editor.putBoolean(keyToBeDeleted, true);
        editor.apply();

        // Kick off deleting everything.
        setUpDocumentDirectory();
        final DocumentModeAssassin assassin = createAssassinForTesting(
                DocumentModeAssassin.STAGE_CHANGE_SETTINGS_DONE, false, true);
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                assassin.addObserver(observer);
                assertEquals(0, deleteStartedCallback.getCallCount());
                assertEquals(0, deleteDoneCallback.getCallCount());
                assassin.deleteDocumentModeData();
            }
        });

        // Make sure the directory is gone and that the prefs are cleared.
        deleteStartedCallback.waitForCallback(0);
        deleteDoneCallback.waitForCallback(0);
        assertFalse(mDocumentModeDirectory.getDataDirectory().exists());
        assertFalse(prefs.contains(keyToBeDeleted));
    }

    /** Creates a DocumentModeAssassin with all of its calls pointing at our mocked classes.
     * @param isMigrationNecessary TODO(dfalcantara):*/
    private DocumentModeAssassinForTesting createAssassinForTesting(
            int stage, boolean runPipeline, final boolean isMigrationNecessary) {
        return new DocumentModeAssassinForTesting(stage, runPipeline) {
            @Override
            public boolean isMigrationNecessary() {
                return isMigrationNecessary;
            }

            @Override
            protected Context getContext() {
                return mContext;
            }
            @Override
            protected File getDocumentDataDirectory() {
                return mDocumentModeDirectory.getDataDirectory();
            }

            @Override
            protected File getTabbedDataDirectory() {
                return mTabbedModeDirectory.getDataDirectory();
            }

            @Override
            protected DocumentTabModel getNormalDocumentTabModel() {
                return mTestTabModel;
            }

            @Override
            protected ActivityDelegate createActivityDelegate() {
                return mActivityDelegate;
            }
        };
    }

    /** Fills in the directory for document mode with a bunch of data. */
    private void setUpDocumentDirectory() throws Exception {
        // Write out all of the TabState files into the document mode directory.
        for (int i = 0; i < TAB_STATE_INFO.length; i++) {
            mDocumentModeDirectory.writeTabStateFile(TAB_STATE_INFO[i]);
        }

        // Write out some random files that should have been ignored by migration.
        writeUselessFileToDirectory(mDocumentModeDirectory.getDataDirectory(), "ignored.file");
        writeUselessFileToDirectory(mDocumentModeDirectory.getDataDirectory(),
                TabState.SAVED_TAB_STATE_FILE_PREFIX + "_unparseable");

    }
}
