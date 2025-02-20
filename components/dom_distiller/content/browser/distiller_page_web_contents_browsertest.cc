// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/dom_distiller/content/browser/distiller_page_web_contents.h"

#include <utility>

#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/dom_distiller/content/browser/distiller_javascript_utils.h"
#include "components/dom_distiller/content/browser/web_contents_main_frame_observer.h"
#include "components/dom_distiller/core/distiller_page.h"
#include "components/dom_distiller/core/proto/distilled_article.pb.h"
#include "components/dom_distiller/core/proto/distilled_page.pb.h"
#include "components/dom_distiller/core/viewer.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"
#include "grit/components_strings.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/dom_distiller_js/dom_distiller.pb.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"

using content::ContentBrowserTest;
using testing::ContainsRegex;
using testing::HasSubstr;
using testing::Not;

namespace {

// Helper class to know how far in the loading process the current WebContents
// has come. It will call the callback either after
// DidCommitProvisionalLoadForFrame or DocumentLoadedInFrame is called for the
// main frame, based on the value of |wait_for_document_loaded|.
class WebContentsMainFrameHelper : public content::WebContentsObserver {
 public:
  WebContentsMainFrameHelper(content::WebContents* web_contents,
                             const base::Closure& callback,
                             bool wait_for_document_loaded)
      : WebContentsObserver(web_contents),
        callback_(callback),
        wait_for_document_loaded_(wait_for_document_loaded) {}

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (wait_for_document_loaded_)
      return;
    if (navigation_handle->HasCommitted() && navigation_handle->IsInMainFrame())
      callback_.Run();
  }

  void DocumentLoadedInFrame(
      content::RenderFrameHost* render_frame_host) override {
    if (wait_for_document_loaded_) {
      if (!render_frame_host->GetParent())
        callback_.Run();
    }
  }

 private:
  base::Closure callback_;
  bool wait_for_document_loaded_;
};

}  // namespace

namespace dom_distiller {

const char* kSimpleArticlePath = "/simple_article.html";
const char* kVideoArticlePath = "/video_article.html";

class DistillerPageWebContentsTest : public ContentBrowserTest {
 public:
  // ContentBrowserTest:
  void SetUpOnMainThread() override {
    if (!DistillerJavaScriptWorldIdIsSet()) {
      SetDistillerJavaScriptWorldId(content::ISOLATED_WORLD_ID_CONTENT_END);
    }
    AddComponentsResources();
    SetUpTestServer();
    ContentBrowserTest::SetUpOnMainThread();
  }

  void DistillPage(const base::Closure& quit_closure, const std::string& url) {
    distiller_page_->DistillPage(
        embedded_test_server()->GetURL(url),
        dom_distiller::proto::DomDistillerOptions(),
        base::Bind(&DistillerPageWebContentsTest::OnPageDistillationFinished,
                   base::Unretained(this), quit_closure));
  }

  void OnPageDistillationFinished(
      base::Closure quit_closure,
      std::unique_ptr<proto::DomDistillerResult> distiller_result,
      bool distillation_successful) {
    distiller_result_ = std::move(distiller_result);
    quit_closure.Run();
  }

  void OnJsExecutionDone(base::Closure callback, const base::Value* value) {
    js_result_.reset(value->DeepCopy());
    callback.Run();
  }

 private:
  void AddComponentsResources() {
    base::FilePath pak_file;
    base::FilePath pak_dir;
#if defined(OS_ANDROID)
    CHECK(PathService::Get(base::DIR_ANDROID_APP_DATA, &pak_dir));
    pak_dir = pak_dir.Append(FILE_PATH_LITERAL("paks"));
#else
    PathService::Get(base::DIR_MODULE, &pak_dir);
#endif  // OS_ANDROID
    pak_file =
        pak_dir.Append(FILE_PATH_LITERAL("components_tests_resources.pak"));
    ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
        pak_file, ui::SCALE_FACTOR_NONE);
  }

  void SetUpTestServer() {
    base::FilePath path;
    PathService::Get(base::DIR_SOURCE_ROOT, &path);
    embedded_test_server()->ServeFilesFromDirectory(
        path.AppendASCII("components/test/data/dom_distiller"));
    embedded_test_server()->ServeFilesFromDirectory(
        path.AppendASCII("components/dom_distiller/core/javascript"));
    ASSERT_TRUE(embedded_test_server()->Start());
  }

 protected:
  void RunUseCurrentWebContentsTest(const std::string& url,
                                    bool expect_new_web_contents,
                                    bool setup_main_frame_observer,
                                    bool wait_for_document_loaded);

  DistillerPageWebContents* distiller_page_;
  std::unique_ptr<proto::DomDistillerResult> distiller_result_;
  std::unique_ptr<base::Value> js_result_;
};

// Use this class to be able to leak the WebContents, which is needed for when
// the current WebContents is used for distillation.
class TestDistillerPageWebContents : public DistillerPageWebContents {
 public:
  TestDistillerPageWebContents(
      content::BrowserContext* browser_context,
      const gfx::Size& render_view_size,
      std::unique_ptr<SourcePageHandleWebContents> optional_web_contents_handle,
      bool expect_new_web_contents)
      : DistillerPageWebContents(browser_context,
                                 render_view_size,
                                 std::move(optional_web_contents_handle)),
        expect_new_web_contents_(expect_new_web_contents),
        new_web_contents_created_(false) {}

  void CreateNewWebContents(const GURL& url) override {
    ASSERT_EQ(true, expect_new_web_contents_);
    new_web_contents_created_ = true;
    DistillerPageWebContents::CreateNewWebContents(url);
  }

  bool new_web_contents_created() { return new_web_contents_created_; }

 private:
  bool expect_new_web_contents_;
  bool new_web_contents_created_;
};

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, BasicDistillationWorks) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kSimpleArticlePath);
  run_loop.Run();

  EXPECT_EQ("Test Page Title", distiller_result_->title());
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("Lorem ipsum"));
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              Not(HasSubstr("questionable content")));
  EXPECT_EQ("", distiller_result_->pagination_info().next_page());
  EXPECT_EQ("", distiller_result_->pagination_info().prev_page());
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, HandlesRelativeLinks) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kSimpleArticlePath);
  run_loop.Run();

  // A relative link should've been updated.
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              ContainsRegex("href=\"http://127.0.0.1:.*/relativelink.html\""));
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("href=\"http://www.google.com/absolutelink.html\""));
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, HandlesRelativeImages) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kSimpleArticlePath);
  run_loop.Run();

  // A relative link should've been updated.
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              ContainsRegex("src=\"http://127.0.0.1:.*/relativeimage.png\""));
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("src=\"http://www.google.com/absoluteimage.png\""));
}


IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, HandlesRelativeVideos) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kVideoArticlePath);
  run_loop.Run();

  // A relative source/track should've been updated.
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              ContainsRegex("src=\"http://127.0.0.1:.*/relative_video.webm\""));
  EXPECT_THAT(
      distiller_result_->distilled_content().html(),
      ContainsRegex("src=\"http://127.0.0.1:.*/relative_track_en.vtt\""));
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("src=\"http://www.google.com/absolute_video.ogg\""));
  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("src=\"http://www.google.com/absolute_track_fr.vtt\""));
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, VisibilityDetection) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  // visble_style.html and invisible_style.html only differ by the visibility
  // internal stylesheet.

  {
    base::RunLoop run_loop;
    DistillPage(run_loop.QuitClosure(), "/visible_style.html");
    run_loop.Run();
    EXPECT_THAT(distiller_result_->distilled_content().html(),
                HasSubstr("Lorem ipsum"));
  }

  {
    base::RunLoop run_loop;
    DistillPage(run_loop.QuitClosure(), "/invisible_style.html");
    run_loop.Run();
    EXPECT_THAT(distiller_result_->distilled_content().html(),
                Not(HasSubstr("Lorem ipsum")));
  }
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       UsingCurrentWebContentsWrongUrl) {
  std::string url("/bogus");
  bool expect_new_web_contents = true;
  bool setup_main_frame_observer = true;
  bool wait_for_document_loaded = true;
  RunUseCurrentWebContentsTest(url,
                               expect_new_web_contents,
                               setup_main_frame_observer,
                               wait_for_document_loaded);
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       UsingCurrentWebContentsNoMainFrameObserver) {
  std::string url(kSimpleArticlePath);
  bool expect_new_web_contents = true;
  bool setup_main_frame_observer = false;
  bool wait_for_document_loaded = true;
  RunUseCurrentWebContentsTest(url,
                               expect_new_web_contents,
                               setup_main_frame_observer,
                               wait_for_document_loaded);
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       DISABLED_UsingCurrentWebContentsNotFinishedLoadingYet) {
  std::string url(kSimpleArticlePath);
  bool expect_new_web_contents = false;
  bool setup_main_frame_observer = true;
  bool wait_for_document_loaded = false;
  RunUseCurrentWebContentsTest(url,
                               expect_new_web_contents,
                               setup_main_frame_observer,
                               wait_for_document_loaded);
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       DISABLED_UsingCurrentWebContentsReadyForDistillation) {
  std::string url(kSimpleArticlePath);
  bool expect_new_web_contents = false;
  bool setup_main_frame_observer = true;
  bool wait_for_document_loaded = true;
  RunUseCurrentWebContentsTest(url,
                               expect_new_web_contents,
                               setup_main_frame_observer,
                               wait_for_document_loaded);
}

void DistillerPageWebContentsTest::RunUseCurrentWebContentsTest(
    const std::string& url,
    bool expect_new_web_contents,
    bool setup_main_frame_observer,
    bool wait_for_document_loaded) {
  content::WebContents* current_web_contents = shell()->web_contents();
  if (setup_main_frame_observer) {
    dom_distiller::WebContentsMainFrameObserver::CreateForWebContents(
        current_web_contents);
  }
  base::RunLoop url_loaded_runner;
  WebContentsMainFrameHelper main_frame_loaded(current_web_contents,
                                               url_loaded_runner.QuitClosure(),
                                               wait_for_document_loaded);
  current_web_contents->GetController().LoadURL(
      embedded_test_server()->GetURL(url),
      content::Referrer(),
      ui::PAGE_TRANSITION_TYPED,
      std::string());
  url_loaded_runner.Run();

  std::unique_ptr<SourcePageHandleWebContents> source_page_handle(
      new SourcePageHandleWebContents(current_web_contents, false));

  TestDistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::move(source_page_handle), expect_new_web_contents);
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kSimpleArticlePath);
  run_loop.Run();

  // Sanity check of distillation process.
  EXPECT_EQ(expect_new_web_contents, distiller_page.new_web_contents_created());
  EXPECT_EQ("Test Page Title", distiller_result_->title());
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       PageDestroyedBeforeFinishDistillation) {

  content::WebContents* current_web_contents = shell()->web_contents();

  dom_distiller::WebContentsMainFrameObserver::CreateForWebContents(
      current_web_contents);

  base::RunLoop url_loaded_runner;
  WebContentsMainFrameHelper main_frame_loaded(current_web_contents,
                                               url_loaded_runner.QuitClosure(),
                                               true);
  current_web_contents->GetController().LoadURL(
      embedded_test_server()->GetURL(kSimpleArticlePath),
      content::Referrer(),
      ui::PAGE_TRANSITION_TYPED,
      std::string());
  url_loaded_runner.Run();

  std::unique_ptr<SourcePageHandleWebContents> source_page_handle(
      new SourcePageHandleWebContents(current_web_contents, false));

  TestDistillerPageWebContents* distiller_page(new TestDistillerPageWebContents(
      current_web_contents->GetBrowserContext(),
      current_web_contents->GetContainerBounds().size(),
      std::move(source_page_handle), false));
  distiller_page_ = distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), kSimpleArticlePath);

  // It can not crash the loop when returning the result.
  delete distiller_page_;

  // Make sure the test ends when it does not crash.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::TimeDelta::FromSeconds(2));

  run_loop.Run();
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest, MarkupInfo) {
  DistillerPageWebContents distiller_page(
      shell()->web_contents()->GetBrowserContext(),
      shell()->web_contents()->GetContainerBounds().size(),
      std::unique_ptr<SourcePageHandleWebContents>());
  distiller_page_ = &distiller_page;

  base::RunLoop run_loop;
  DistillPage(run_loop.QuitClosure(), "/markup_article.html");
  run_loop.Run();

  EXPECT_THAT(distiller_result_->distilled_content().html(),
              HasSubstr("Lorem ipsum"));
  EXPECT_EQ("Marked-up Markup Test Page Title", distiller_result_->title());

  const proto::MarkupInfo markup_info = distiller_result_->markup_info();
  EXPECT_EQ("Marked-up Markup Test Page Title", markup_info.title());
  EXPECT_EQ("Article", markup_info.type());
  EXPECT_EQ("http://test/markup.html", markup_info.url());
  EXPECT_EQ("This page tests Markup Info.", markup_info.description());
  EXPECT_EQ("Whoever Published", markup_info.publisher());
  EXPECT_EQ("Copyright 2000-2014 Whoever Copyrighted", markup_info.copyright());
  EXPECT_EQ("Whoever Authored", markup_info.author());

  const proto::MarkupArticle markup_article = markup_info.article();
  EXPECT_EQ("Whatever Section", markup_article.section());
  EXPECT_EQ("July 23, 2014", markup_article.published_time());
  EXPECT_EQ("2014-07-23T23:59", markup_article.modified_time());
  EXPECT_EQ("", markup_article.expiration_time());
  ASSERT_EQ(1, markup_article.authors_size());
  EXPECT_EQ("Whoever Authored", markup_article.authors(0));

  ASSERT_EQ(2, markup_info.images_size());

  const proto::MarkupImage markup_image1 = markup_info.images(0);
  EXPECT_EQ("http://test/markup1.jpeg", markup_image1.url());
  EXPECT_EQ("https://test/markup1.jpeg", markup_image1.secure_url());
  EXPECT_EQ("jpeg", markup_image1.type());
  EXPECT_EQ("", markup_image1.caption());
  EXPECT_EQ(600, markup_image1.width());
  EXPECT_EQ(400, markup_image1.height());

  const proto::MarkupImage markup_image2 = markup_info.images(1);
  EXPECT_EQ("http://test/markup2.gif", markup_image2.url());
  EXPECT_EQ("https://test/markup2.gif", markup_image2.secure_url());
  EXPECT_EQ("gif", markup_image2.type());
  EXPECT_EQ("", markup_image2.caption());
  EXPECT_EQ(1000, markup_image2.width());
  EXPECT_EQ(600, markup_image2.height());
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       TestNoContentDoesNotCrash) {
  const std::string no_content =
      l10n_util::GetStringUTF8(IDS_DOM_DISTILLER_VIEWER_NO_DATA_CONTENT);

  {  // Test zero pages.
    std::unique_ptr<DistilledArticleProto> article_proto(
        new DistilledArticleProto());
    std::string js = viewer::GetUnsafeArticleContentJs(article_proto.get());
    EXPECT_THAT(js, HasSubstr(no_content));
  }

  {  // Test empty content.
    std::unique_ptr<DistilledArticleProto> article_proto(
        new DistilledArticleProto());
    (*(article_proto->add_pages())).set_html("");
    std::string js = viewer::GetUnsafeArticleContentJs(article_proto.get());
    EXPECT_THAT(js, HasSubstr(no_content));
  }
}

IN_PROC_BROWSER_TEST_F(DistillerPageWebContentsTest,
                       TestPinch) {
  // Load the test file in content shell and wait until it has fully loaded.
  content::WebContents* web_contents = shell()->web_contents();
  dom_distiller::WebContentsMainFrameObserver::CreateForWebContents(
      web_contents);
  base::RunLoop url_loaded_runner;
  WebContentsMainFrameHelper main_frame_loaded(web_contents,
                                               url_loaded_runner.QuitClosure(),
                                               true);
  web_contents->GetController().LoadURL(
      embedded_test_server()->GetURL("/pinch_tester.html"),
      content::Referrer(),
      ui::PAGE_TRANSITION_TYPED,
      std::string());
  url_loaded_runner.Run();

  // Execute the JS to run the tests, and wait until it has finished.
  base::RunLoop run_loop;
  web_contents->GetMainFrame()->ExecuteJavaScriptForTests(
      base::UTF8ToUTF16("(function() {return pinchtest.run();})();"),
      base::Bind(&DistillerPageWebContentsTest::OnJsExecutionDone,
                 base::Unretained(this), run_loop.QuitClosure()));
  run_loop.Run();

  // Convert to dictionary and parse the results.
  const base::DictionaryValue* dict;
  ASSERT_TRUE(js_result_);
  ASSERT_TRUE(js_result_->GetAsDictionary(&dict));

  ASSERT_TRUE(dict->HasKey("success"));
  bool success;
  ASSERT_TRUE(dict->GetBoolean("success", &success));
  EXPECT_TRUE(success);
}

}  // namespace dom_distiller
