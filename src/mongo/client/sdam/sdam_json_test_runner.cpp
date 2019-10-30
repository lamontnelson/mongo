/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */
#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "mongo/bson/json.h"
#include "mongo/client/sdam/topology_manager.h"
#include "mongo/util/clock_source_mock.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;
using namespace mongo::sdam;

namespace mongo::sdam {

class ArgParser {
public:
    constexpr static auto kSourceDirOptionLong = "source-dir";
    constexpr static auto kSourceDirOptionShort = "s";
    constexpr static auto kSourceDirDefault = ".";

    constexpr static auto kFilterOptionLong = "filter";
    constexpr static auto kFilterOptionShort = "f";

    po::variables_map values;
    std::string SourceDirectory;
    std::vector<std::string> TestFilters;

    ArgParser(int argc, char* argv[]) {
        po::options_description optionsDescription("Arguments");
        try {
            optionsDescription.add_options()("help", "help")(
                optionName(kSourceDirOptionShort, kSourceDirOptionLong).c_str(),
                po::value<std::string>(&SourceDirectory)->default_value(kSourceDirDefault),
                "set source directory")(optionName(kFilterOptionLong, kFilterOptionShort).c_str(),
                                        po::value<std::vector<std::string>>(&TestFilters),
                                        "filter tests to run");
            po::store(po::parse_command_line(argc, argv, optionsDescription), values);
            po::notify(values);

            if (helpRequested()) {
                printHelpAndExit(argv[0], optionsDescription);
            }
        } catch (const boost::program_options::unknown_option& ex) {
            std::cout << "Error while parsing command-line arguments!" << std::endl
                      << ex.what() << std::endl
                      << std::endl;
            printHelpAndExit(argv[0], optionsDescription);
        }
    }

private:
    std::string optionName(const char* shortName, const char* longName) {
        static auto format = boost::format("%1%,%2%");
        format % longName;
        format % shortName;
        return format.str();
    }

    bool helpRequested() {
        return values.count("help") > 0;
    }

    void printHelpAndExit(char* programName, const po::options_description& desc) {
        std::cout << programName << ":" << std::endl << desc << std::endl;
        std::exit(1);
    }
};

struct JsonTestResult {};

class TestCasePhase {
public:
	TestCasePhase(BSONObj phase) {
		auto bsonResponses = phase.getField("responses").Array();

		for (auto& response : bsonResponses) {
			auto pair = response.Array();
			auto address = pair[0].String();
			auto bsonIsMaster = pair[1].Obj();
			BSONObjBuilder isMasterWithAddress(bsonIsMaster);
			isMasterWithAddress.append("address", address);
			auto isMasterOutcome = IsMasterOutcome(address, isMasterWithAddress.obj(), kLatency);
			responses.push_back(isMasterOutcome);
		}
		topologyOutcome = phase["outcome"].Obj();

		testResult = validate();
	}

	struct ValidateResult {
		bool success;
		// vector of errorItem, errorString pairs
		// the vector only has items if success is false
		std::vector<std::pair<std::string, std::string>> errorDescriptions;
	};

    void validateServers(const TopologyDescriptionPtr topologyDescription, const BSONObj bsonServers, ValidateResult &result) {
        auto actualNumServers = topologyDescription->getServers().size();
        auto expectedNumServers = bsonServers.getFieldNames<std::unordered_set<std::string>>().size();

        if (actualNumServers != expectedNumServers) {
            result.success = false;
            std::stringstream errorMessage;
            errorMessage << "expected " << expectedNumServers << " server in topology description. actual was " << actualNumServers << ".";
            result.errorDescriptions.push_back(std::make_pair("servers", errorMessage.str()));
            errorMessage.clear();
        }
    }

    void validateTopologyDescription(const TopologyDescriptionPtr topologyDescription, const BSONObj bsonTopologyDescription, ValidateResult &result) {
    }

    std::vector<ServerAddress> getSeedList() {
        std::vector<ServerAddress> result;
        for (const auto& isMasterOutcome : responses) {
            result.push_back(isMasterOutcome.getServer());
        }
        return result;
    }

    ValidateResult validate() {
        const auto& seedList = getSeedList();

        std::unique_ptr<SdamConfiguration> config;
        if (seedList.size() > 0) {
            config = std::make_unique<SdamConfiguration>(seedList);
        } else {
            config = std::make_unique<SdamConfiguration>();
        }
	    auto clockSource = std::make_unique<ClockSourceMock>();
		TopologyManager topology(*config, clockSource.get());
		ValidateResult result;

		for (auto response : responses) {
		    topology.onServerDescription(response);
		}

		validateServers(topology.getTopologyDescription(), topologyOutcome["servers"].Obj(), result);
		validateTopologyDescription(topology.getTopologyDescription(), topologyOutcome, result);

        return result;
	}

    const ValidateResult &getTestResult() const {
        return testResult;
    }
private:
	constexpr static auto kLatency = mongo::Milliseconds(1);

	std::vector<IsMasterOutcome> responses;
	BSONObj topologyOutcome;
	ValidateResult testResult;
};

class JsonTestCase {
public:
    JsonTestCase(fs::path testFilePath) {
        parseTest(testFilePath);
    }

    void parseTest(fs::path testFilePath) {
        using namespace std;

        ifstream testFile(testFilePath.string());
        ostringstream json;
        json << testFile.rdbuf();

        _jsonTest = fromjson(json.str());
        _name = _jsonTest.getStringField("description");

        int phase = 0;
        for (auto bsonPhase : _jsonTest["phases"].Array()) {
            testPhases.push_back(TestCasePhase(bsonPhase.Obj()));
            if (!testPhases.back().getTestResult().success) {
                std::cout << "phase "<< phase << " failed:" << std::endl;
                const auto& testResult = testPhases.back().getTestResult();
                if (!testResult.success) {
                    for (const auto& fieldMsgpair : testResult.errorDescriptions) {
                        std::cout << "ERROR " << fieldMsgpair.first << ": " << fieldMsgpair.second << std::endl;
                    }
                }
            }
            phase++;
        }

        std::cout << "done test: " << testFilePath.string() << ": " << _name << std::endl;
    }

private:
    BSONObj _jsonTest;
    std::string _name;
    std::vector<TestCasePhase> testPhases;

public:
    const std::string& Name() const {
        return _name;
    }
};

class SdamJsonTestRunner {
public:
    SdamJsonTestRunner(std::string testDirectory, std::vector<std::string> testFilters)
        : _testFiles(scanTestFiles(testDirectory, testFilters)) {}

    JsonTestResult doTest(JsonTestCase testCase) {
        return JsonTestResult();
    }

    std::map<std::string, JsonTestResult> runTests() {
        const auto testFiles = getTestFiles();
        auto results = std::map<std::string, JsonTestResult>();
        for (auto jsonTest : testFiles) {
            auto testCase = JsonTestCase(jsonTest);
            results[testCase.Name()] = doTest(testCase);
        }
        return results;
    }

    const std::vector<fs::path>& getTestFiles() const {
        return _testFiles;
    }

private:
    std::vector<fs::path> scanTestFiles(std::string testDirectory,
                                        std::vector<std::string> filters) {
        std::vector<fs::path> results;
        for (const auto& entry : fs::recursive_directory_iterator(testDirectory)) {
            if (matchesFilter(entry, filters) && !fs::is_directory(entry)) {
                results.push_back(entry.path());
            }
        }
        return results;
    }

    bool matchesFilter(const fs::directory_entry& entry, std::vector<std::string> filters) {
        if (filters.size() == 0) {
            return true;
        }

        for (const auto& filter : filters) {
            if (entry.path().filename().string().find(filter) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    std::vector<fs::path> _testFiles;
};
};  // namespace mongo::sdam

int main(int argc, char* argv[]) {
    ArgParser args(argc, argv);
    SdamJsonTestRunner testRunner(args.SourceDirectory, args.TestFilters);
    testRunner.runTests();
    return 0;
}
