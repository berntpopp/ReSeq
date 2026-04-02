#include "RegressionTest.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

void reseq::RegressionTest::Register() {
    // Guarantees that library is included
}

namespace reseq {

// --- replaceN command ---

TEST_F(RegressionTest, ReplaceN) {
    auto ref_in = test_dir_ / "reference-test.fa";
    auto ref_out = tmp_dir_ / "replaced.fa";
    auto expected = expected_dir_ / "replaceN.fa";

    int rc = RunReseq("replaceN -r " + ref_in.string() + " -R " + ref_out.string() + " --seed 42");
    ASSERT_EQ(0, rc) << "replaceN exited with code " << rc;
    ASSERT_TRUE(std::filesystem::exists(ref_out)) << "Output file not created: " << ref_out;

    ExpectTextFilesEqual(expected, ref_out);
}

// --- queryProfile from small BAM (generated at test time) ---

TEST_F(RegressionTest, QueryProfileMaxReadLength) {
    auto profile = GenerateEcoliProfile();
    ASSERT_TRUE(std::filesystem::exists(profile)) << "Profile not generated";

    std::string output = RunReseqCapture("queryProfile -s " + profile.string() + " --maxReadLength");

    // Read expected
    std::ifstream exp_stream(expected_dir_ / "queryProfile_maxReadLength.txt");
    ASSERT_TRUE(exp_stream.is_open());
    std::string expected((std::istreambuf_iterator<char>(exp_stream)), std::istreambuf_iterator<char>());

    EXPECT_EQ(expected, output) << "maxReadLength output mismatch";
}

TEST_F(RegressionTest, QueryProfileMaxLenDeletion) {
    auto profile = GenerateEcoliProfile();
    ASSERT_TRUE(std::filesystem::exists(profile)) << "Profile not generated";

    std::string output = RunReseqCapture("queryProfile -s " + profile.string() + " --maxLenDeletion");

    std::ifstream exp_stream(expected_dir_ / "queryProfile_maxLenDeletion.txt");
    ASSERT_TRUE(exp_stream.is_open());
    std::string expected((std::istreambuf_iterator<char>(exp_stream)), std::istreambuf_iterator<char>());

    EXPECT_EQ(expected, output) << "maxLenDeletion output mismatch";
}

TEST_F(RegressionTest, QueryProfileFragLenBias) {
    auto profile = GenerateEcoliProfile();
    ASSERT_TRUE(std::filesystem::exists(profile)) << "Profile not generated";

    auto actual_file = tmp_dir_ / "fragLenBias.tsv";
    int rc = RunReseq("queryProfile -s " + profile.string() + " --fragLenBias " + actual_file.string());
    ASSERT_EQ(0, rc) << "queryProfile --fragLenBias exited with code " << rc;

    ExpectTextFilesEqual(expected_dir_ / "queryProfile_fragLenBias.tsv", actual_file);
}

// --- queryProfile from Zenodo profile (skip if not downloaded) ---

TEST_F(RegressionTest, RealProfileMaxReadLength) {
    auto profile = ZenodoProfile();
    if (profile.empty()) {
        GTEST_SKIP() << "Zenodo profile not available (test/data/Hs-Nova-TruSeq.reseq)";
    }

    std::string output = RunReseqCapture("queryProfile -s " + profile.string() + " --maxReadLength");

    std::ifstream exp_stream(expected_dir_ / "queryProfile_real_maxReadLength.txt");
    ASSERT_TRUE(exp_stream.is_open());
    std::string expected((std::istreambuf_iterator<char>(exp_stream)), std::istreambuf_iterator<char>());

    EXPECT_EQ(expected, output) << "Real profile maxReadLength mismatch";
}

TEST_F(RegressionTest, RealProfileMaxLenDeletion) {
    auto profile = ZenodoProfile();
    if (profile.empty()) {
        GTEST_SKIP() << "Zenodo profile not available (test/data/Hs-Nova-TruSeq.reseq)";
    }

    std::string output = RunReseqCapture("queryProfile -s " + profile.string() + " --maxLenDeletion");

    std::ifstream exp_stream(expected_dir_ / "queryProfile_real_maxLenDeletion.txt");
    ASSERT_TRUE(exp_stream.is_open());
    std::string expected((std::istreambuf_iterator<char>(exp_stream)), std::istreambuf_iterator<char>());

    EXPECT_EQ(expected, output) << "Real profile maxLenDeletion mismatch";
}

TEST_F(RegressionTest, RealProfileFragLenBias) {
    auto profile = ZenodoProfile();
    if (profile.empty()) {
        GTEST_SKIP() << "Zenodo profile not available (test/data/Hs-Nova-TruSeq.reseq)";
    }

    auto actual_file = tmp_dir_ / "real_fragLenBias.tsv";
    int rc = RunReseq("queryProfile -s " + profile.string() + " --fragLenBias " + actual_file.string());
    ASSERT_EQ(0, rc) << "queryProfile --fragLenBias exited with code " << rc;

    ExpectTextFilesEqual(expected_dir_ / "queryProfile_real_fragLenBias.tsv", actual_file);
}

// --- Error handling ---

TEST_F(RegressionTest, ErrorBadCommand) {
    int rc = RunReseq("nonsenseCommand");
    EXPECT_NE(0, rc) << "Expected non-zero exit for unknown command";
}

TEST_F(RegressionTest, ErrorMissingRef) {
    int rc = RunReseq("replaceN -r /nonexistent/path/ref.fa -R " + (tmp_dir_ / "out.fa").string());
    EXPECT_NE(0, rc) << "Expected non-zero exit for missing reference";
}

TEST_F(RegressionTest, VersionOutput) {
    std::string output = RunReseqCaptureStderr("--version");
    EXPECT_NE(std::string::npos, output.find("ReSeq")) << "Version output should contain 'ReSeq', got: " << output;
}

} // namespace reseq
