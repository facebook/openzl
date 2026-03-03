// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Corpus {
    std::string name;
    std::string profile;
};

} // namespace

std::string getHgRoot()
{
    std::string hgRoot;
    FILE* pipe = popen("hg root", "r");
    if (!pipe) {
        throw std::runtime_error("popen failed!");
    }
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        hgRoot += buffer;
    }
    pclose(pipe);
    // Remove trailing whitespace/newline
    hgRoot.erase(hgRoot.find_last_not_of(" \n\r\t") + 1);
    return hgRoot;
}

void moveFiles(
        const std::filesystem::path& sourceDir,
        const std::filesystem::path& targetDir,
        const std::filesystem::path& splitTxt)
{
    std::ifstream in{ splitTxt };
    std::string temp;
    std::vector<std::string> filenames;
    while (std::getline(in, temp)) {
        filenames.push_back(temp);
    }

    for (const auto& name : filenames) {
        auto srcPath      = sourceDir / name;
        auto dstPath      = targetDir / name;
        std::string mvCmd = "mv " + srcPath.string() + " " + dstPath.string();
        std::system(mvCmd.c_str());
    }
}

std::tuple<std::filesystem::path, std::filesystem::path, std::filesystem::path>
split(const std::filesystem::path& workingDir,
      const std::filesystem::path& splitRoot,
      const std::filesystem::path& corpusDir)
{
    auto trainDir = workingDir / "train";
    std::filesystem::create_directory(trainDir);
    auto trainSrcs = splitRoot / "train.txt";
    moveFiles(corpusDir, trainDir, trainSrcs);
    std::cout << "====== Moved training files to " << trainDir.string()
              << std::endl;

    auto testDir = workingDir / "test";
    std::filesystem::create_directory(testDir);
    auto testSrcs = splitRoot / "test.txt";
    moveFiles(corpusDir, testDir, testSrcs);
    std::cout << "====== Moved testing files to " << testDir.string()
              << std::endl;
    return { workingDir, trainDir, testDir };
}

std::filesystem::path collectStats(
        const std::filesystem::path& rootDir,
        const std::filesystem::path& workingDir,
        const std::filesystem::path& corpusDir,
        const std::filesystem::path& corpusSplitDir,
        const std::string& profile)
{
    std::cout << "==== Creating test/train split" << std::endl;
    const auto [resultDir, trainDir, testDir] =
            split(workingDir, corpusSplitDir, corpusDir);
    const auto compressorPath = resultDir / "trained.zlc";
    const auto resultsPath    = resultDir / "results.csv";

    std::cout << "==== Training on files in " << trainDir.string() << std::endl;
    std::string trainCmd = "cd " + rootDir.string()
                + " && buck2 run @//mode/opt cli:zli -- train -t greedy --use-all-samples"
                 " -p " + profile + " " + trainDir.string() +
                 " -o " + compressorPath.string();
    std::cout << "==== Running command: '" << trainCmd << "'" << std::endl;
    std::system(trainCmd.c_str());

    std::cout << "==== Benchmarking on files in " << testDir.string()
              << std::endl;
    std::string testCmd = "cd " + rootDir.string()
            + " && buck2 run @//mode/opt cli:zli -- benchmark -c "
            + compressorPath.string() + " --num-iters 1 --output-csv "
            + resultsPath.string() + " " + testDir.string();
    std::cout << "==== Running command: '" << testCmd << "'" << std::endl;
    std::system(testCmd.c_str());
    return resultsPath;
}

int main()
{
    std::string hgRoot = getHgRoot();
    std::string thisFile =
            __FILE__; // in fbcode this will be relative to fbsource root, i.e.
                      // fbcode/data_compression/.../runner.cpp
    thisFile                       = hgRoot + "/" + thisFile;
    std::filesystem::path thisPath = thisFile;
    std::filesystem::path thisDir  = thisPath.parent_path();
    std::filesystem::path rootDir =
            thisPath.parent_path().parent_path().parent_path();

    std::vector<Corpus> corpora = {
        { .name = "binance_canonical", .profile = "parquet" },
        { .name = "tlc_canonical", .profile = "parquet" },
        { .name = "rea6_precip", .profile = "ace" },
        { .name = "era5_flux", .profile = "ace" },
        { .name = "era5_precip", .profile = "ace" },
        { .name = "era5_pressure", .profile = "ace" },
        { .name = "era5_snow", .profile = "ace" },
        { .name = "era5_wind", .profile = "ace" },
        { .name = "ppmf_unit", .profile = "csv" },
        { .name = "ppmf_person", .profile = "csv" },
        { .name = "psam_p", .profile = "csv" },
        { .name = "psam_h", .profile = "csv" },
    };

    std::cout << "OpenZL Benchmark Runner" << std::endl;

    // create a scratch directory to work in
    std::filesystem::path scratchDir = thisDir / "_bench";
    std::filesystem::create_directory(scratchDir);
    std::cout << "Using scratch directory: " << scratchDir.string() << "\n"
              << "Benchmarking " << corpora.size() << " corpora:\n";
    for (const auto& corpus : corpora) {
        std::cout << "  " << corpus.name << " -- " << corpus.profile << "\n";
    }
    std::cout << std::endl;
    for (const auto& [corpusName, corpusProfile] : corpora) {
        std::cout << "== Starting benchmark on " << corpusName << std::endl;
        std::string downloadCmd = "cd " + rootDir.string()
                + " && ./corpus_download.sh " + corpusName;
        std::cout << "==== Running command: '" << downloadCmd << "'"
                  << std::endl;
        std::system(downloadCmd.c_str());
        auto corpusDir = rootDir / "_corpus" / corpusName;

        // shuffle around files
        const auto corpusSplitDir = thisDir / corpusName;
        const auto workingDir     = scratchDir / corpusName;
        std::filesystem::create_directory(workingDir);

        // some corpora have a high/low split based on the data. So we will
        // collect data on both and merge them together for the final results
        const auto high = corpusSplitDir / "high";
        const auto low  = corpusSplitDir / "low";
        if (std::filesystem::is_directory(high)
            && std::filesystem::is_directory(low)) {
            const auto highWorkingDir = workingDir / "high";
            std::filesystem::create_directory(highWorkingDir);
            const auto highCsvPath = collectStats(
                    rootDir, highWorkingDir, corpusDir, high, corpusProfile);
            std::cout << "==== Wrote [high] results to " << highCsvPath.string()
                      << std::endl;
            const auto lowWorkingDir = workingDir / "low";
            std::filesystem::create_directory(lowWorkingDir);
            const auto lowCsvPath = collectStats(
                    rootDir, lowWorkingDir, corpusDir, low, corpusProfile);
            std::cout << "==== Wrote [low] results to " << lowCsvPath.string()
                      << std::endl;
        } else {
            const auto csvPath = collectStats(
                    rootDir,
                    workingDir,
                    corpusDir,
                    corpusSplitDir,
                    corpusProfile);
            std::cout << "==== Wrote results to " << csvPath.string()
                      << std::endl;
        }
    }

    return 0;
}
