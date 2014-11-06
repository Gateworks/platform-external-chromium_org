// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/data_driven_test.h"
#include "components/autofill/core/browser/form_structure.h"
#include "url/gurl.h"

namespace autofill {
namespace {

const base::FilePath::CharType kTestName[] = FILE_PATH_LITERAL("heuristics");

// Convert the |html| snippet to a data URI.
GURL HTMLToDataURI(const std::string& html) {
  return GURL(std::string("data:text/html;charset=utf-8,") + html);
}

const base::FilePath& GetTestDataDir() {
  CR_DEFINE_STATIC_LOCAL(base::FilePath, dir, ());
  if (dir.empty())
    PathService::Get(chrome::DIR_TEST_DATA, &dir);
  return dir;
}

}  // namespace

// A data-driven test for verifying Autofill heuristics. Each input is an HTML
// file that contains one or more forms. The corresponding output file lists the
// heuristically detected type for eachfield.
class FormStructureBrowserTest : public InProcessBrowserTest,
                                 public DataDrivenTest {
 protected:
  FormStructureBrowserTest();
  ~FormStructureBrowserTest() override;

  // DataDrivenTest:
  void GenerateResults(const std::string& input, std::string* output) override;

  // Serializes the given |forms| into a string.
  std::string FormStructuresToString(const std::vector<FormStructure*>& forms);

 private:
  DISALLOW_COPY_AND_ASSIGN(FormStructureBrowserTest);
};

FormStructureBrowserTest::FormStructureBrowserTest()
    : DataDrivenTest(GetTestDataDir()) {
}

FormStructureBrowserTest::~FormStructureBrowserTest() {
}

void FormStructureBrowserTest::GenerateResults(const std::string& input,
                                               std::string* output) {
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::NavigateToURL(browser(),
                                                       HTMLToDataURI(input)));

  ContentAutofillDriver* autofill_driver =
      ContentAutofillDriver::FromWebContents(
          browser()->tab_strip_model()->GetActiveWebContents());
  ASSERT_NE(static_cast<ContentAutofillDriver*>(NULL), autofill_driver);
  AutofillManager* autofill_manager = autofill_driver->autofill_manager();
  ASSERT_NE(static_cast<AutofillManager*>(NULL), autofill_manager);
  std::vector<FormStructure*> forms = autofill_manager->form_structures_.get();
  *output = FormStructuresToString(forms);
}

std::string FormStructureBrowserTest::FormStructuresToString(
    const std::vector<FormStructure*>& forms) {
  std::string forms_string;
  for (const FormStructure* form : forms) {
    for (const AutofillField* field : *form) {
      forms_string += field->Type().ToString();
      forms_string += " | " + base::UTF16ToUTF8(field->name);
      forms_string += " | " + base::UTF16ToUTF8(field->label);
      forms_string += " | " + base::UTF16ToUTF8(field->value);
      forms_string += " | " + field->section();
      forms_string += "\n";
    }
  }
  return forms_string;
}

// Heuristics tests time out on Windows (http://crbug.com/85276) and Chrome OS
// (http://crbug.com/423791).
#if defined(OS_WIN) || defined(OS_CHROMEOS)
#define MAYBE_DataDrivenHeuristics(n) DISABLED_DataDrivenHeuristics##n
#else
#define MAYBE_DataDrivenHeuristics(n) DataDrivenHeuristics##n
#endif
IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(00)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("00_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest, DataDrivenHeuristics01) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("01_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(02)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("02_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(03)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("03_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(04)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("04_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(05)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("05_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(06)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("06_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(07)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("07_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(08)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("08_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(09)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("09_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(10)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("10_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(11)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("11_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(12)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("12_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(13)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("13_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(14)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("14_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(15)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("15_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(16)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("16_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(17)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("17_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(20)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("20_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(21)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("21_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(22)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("22_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

IN_PROC_BROWSER_TEST_F(FormStructureBrowserTest,
    MAYBE_DataDrivenHeuristics(23)) {
  const base::FilePath::CharType kFileNamePattern[] =
      FILE_PATH_LITERAL("23_*.html");
  RunDataDrivenTest(GetInputDirectory(kTestName),
                    GetOutputDirectory(kTestName),
                    kFileNamePattern);
}

}  // namespace autofill
