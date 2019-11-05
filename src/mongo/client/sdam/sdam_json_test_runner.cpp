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
#include <boost/optional/optional_io.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <memory>

#include "mongo/bson/json.h"
#include "mongo/client/mongo_uri.h"
#include "mongo/client/sdam/topology_manager.h"
#include "mongo/util/clock_source_mock.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;
using namespace mongo::sdam;

namespace mongo::sdam {
std::string banner(const std::string text) {
    std::stringstream output;
    const auto border = std::string(text.size(), '-');
    output << border << std::endl << text << std::endl << border << std::endl;
    return output.str();
}

class ArgParser {
public:
    ArgParser(int argc, char* argv[]) {
        po::options_description optionsDescription("Arguments");
        try {
            optionsDescription.add_options()("help", "help")(
                optionName(kSourceDirOptionShort, kSourceDirOptionLong).c_str(),
                po::value<std::string>(&SourceDirectory)->default_value(kSourceDirDefault),
                "set source directory")(optionName(kFilterOptionShort, kFilterOptionLong).c_str(),
                                        po::value<std::vector<std::string>>(&TestFilters),
                                        "filter tests to run");
            po::store(po::parse_command_line(argc, argv, optionsDescription), Values);
            po::notify(Values);

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

    po::variables_map Values;
    std::string SourceDirectory;
    std::vector<std::string> TestFilters;

private:
    constexpr static auto kSourceDirOptionLong = "source-dir";
    constexpr static auto kSourceDirOptionShort = "s";
    constexpr static auto kSourceDirDefault = ".";

    constexpr static auto kFilterOptionLong = "filter";
    constexpr static auto kFilterOptionShort = "f";

    std::string optionName(const char* shortName, const char* longName) {
        static auto format = boost::format("%1%,%2%");
        format % longName;
        format % shortName;
        return format.str();
    }

    bool helpRequested() {
        return Values.count("help") > 0;
    }

    void printHelpAndExit(char* programName, const po::options_description& desc) {
        std::cout << programName << ":" << std::endl << desc << std::endl;
        std::exit(1);
    }
};

// This class is responsible for parsing and executing a single 'phase' of the json test
class TestCasePhase {
public:
    TestCasePhase(int phaseNum, MongoURI uri, BSONObj phase) : _testUri(uri), _phaseNum(phaseNum) {
        auto bsonResponses = phase.getField("responses").Array();
        for (auto& response : bsonResponses) {
            const auto pair = response.Array();
            const auto address = pair[0].String();
            const auto bsonIsMaster = pair[1].Obj();

            IsMasterOutcome* isMasterOutcome;
            if (bsonIsMaster.binaryEqual(BSONObjBuilder().obj())) {
                isMasterOutcome = new IsMasterOutcome(address, "network error");
            } else {
                isMasterOutcome = new IsMasterOutcome(address, bsonIsMaster, kLatency);
            }

            _isMasterResponses.push_back(*isMasterOutcome);
        }
        _topologyOutcome = phase["outcome"].Obj();
    }

    // pair of error subject & error description
    using TestPhaseError = std::pair<std::string, std::string>;
    struct PhaseResult {
        bool success;
        // the vector only has items if success is false
        std::vector<TestPhaseError> errorDescriptions;
        int phaseNumber;
    };

    PhaseResult execute(TopologyManager& topology) const {
        PhaseResult testResult{true, {}, _phaseNum};

        for (auto response : _isMasterResponses) {
            auto descriptionStr =
                (response.getResponse()) ? response.getResponse()->toString() : "[ Network Error ]";
            std::cout << "Sending server description: " << response.getServer() << " : "
                      << descriptionStr << std::endl;
            topology.onServerDescription(response);
        }

        validateServers(
            topology.getTopologyDescription(), _topologyOutcome["servers"].Obj(), testResult);
        validateTopologyDescription(
            topology.getTopologyDescription(), _topologyOutcome, testResult);

        return testResult;
    }

    int getPhaseNum() const {
        return _phaseNum;
    }

private:
    template <typename T, typename U>
    std::string errorMessageNotEqual(T expected, U actual) const {
        std::stringstream errorMessage;
        errorMessage << "expected '" << actual << "' to equal '" << expected << "'";
        return errorMessage.str();
    }

    std::string serverDescriptionFieldName(const ServerDescriptionPtr serverDescription,
                                           std::string field) const {
        std::stringstream name;
        name << "(" << serverDescription->getAddress() << ") " << field;
        return name.str();
    }

    std::string topologyDescriptionFieldName(std::string field) const {
        std::stringstream name;
        name << "(topologyDescription) " << field;
        return name.str();
    }

    void validateServerField(const ServerDescriptionPtr& serverDescription,
                             const BSONElement& expectedField,
                             PhaseResult& result) const {
        const auto serverAddress = serverDescription->getAddress();

        std::string fieldName = expectedField.fieldName();
        if (fieldName == "type") {
            ServerType expectedServerType;
            auto serverTypeParseStatus = parseServerType(expectedField.String());
            if (!serverTypeParseStatus.isOK()) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(serverDescriptionFieldName(serverDescription, "type"),
                                   serverTypeParseStatus.getStatus().toString());
                result.errorDescriptions.push_back(errorDescription);
                return;
            }
            expectedServerType = serverTypeParseStatus.getValue();

            if (expectedServerType != serverDescription->getType()) {
                result.success = false;
                auto errorDescription = std::make_pair(
                    serverDescriptionFieldName(serverDescription, "type"),
                    errorMessageNotEqual(expectedServerType, serverDescription->getType()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "setName") {
            boost::optional<std::string> expectedSetName;
            if (expectedField.type() != BSONType::jstNULL) {
                expectedSetName = expectedField.String();
            }
            if (expectedSetName != serverDescription->getSetName()) {
                result.success = false;
                auto errorDescription = std::make_pair(
                    serverDescriptionFieldName(serverDescription, "setName"),
                    errorMessageNotEqual(expectedSetName, serverDescription->getSetName()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "setVersion") {
            boost::optional<int> expectedSetVersion;
            if (expectedField.type() != BSONType::jstNULL) {
                expectedSetVersion = expectedField.numberInt();
            }
            if (expectedSetVersion != serverDescription->getSetVersion()) {
                result.success = false;
                auto errorDescription = std::make_pair(
                    serverDescriptionFieldName(serverDescription, "setVersion"),
                    errorMessageNotEqual(expectedSetVersion, serverDescription->getSetVersion()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "electionId") {
            boost::optional<OID> expectedElectionId;
            if (expectedField.type() != BSONType::jstNULL) {
                expectedElectionId = expectedField.OID();
            }
            if (expectedElectionId != serverDescription->getElectionId()) {
                result.success = false;
                auto errorDescription = std::make_pair(
                    serverDescriptionFieldName(serverDescription, "electionId"),
                    errorMessageNotEqual(expectedElectionId, serverDescription->getElectionId()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "logicalSessionTimeoutMinutes") {
            boost::optional<int> expectedLSTM;
            if (expectedField.type() != BSONType::jstNULL) {
                expectedLSTM = expectedField.numberInt();
            }
            if (expectedLSTM != serverDescription->getLogicalSessionTimeoutMinutes()) {
                result.success = false;
                auto errorDescription = std::make_pair(
                    serverDescriptionFieldName(serverDescription, "logicalSessionTimeoutMinutes"),
                    errorMessageNotEqual(expectedLSTM,
                                         serverDescription->getLogicalSessionTimeoutMinutes()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "minWireVersion") {
            int expectedMinWireVersion = expectedField.numberInt();
            if (expectedMinWireVersion != serverDescription->getMinWireVersion()) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(serverDescriptionFieldName(serverDescription, "minWireVersion"),
                                   errorMessageNotEqual(expectedMinWireVersion,
                                                        serverDescription->getMinWireVersion()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else if (fieldName == "maxWireVersion") {
            int expectedMaxWireVersion = expectedField.numberInt();
            if (expectedMaxWireVersion != serverDescription->getMaxWireVersion()) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(serverDescriptionFieldName(serverDescription, "maxWireVersion"),
                                   errorMessageNotEqual(expectedMaxWireVersion,
                                                        serverDescription->getMinWireVersion()));
                result.errorDescriptions.push_back(errorDescription);
            }
        } else {
            MONGO_UNREACHABLE;
        }
    }

    void validateServers(const TopologyDescriptionPtr topologyDescription,
                         const BSONObj bsonServers,
                         PhaseResult& result) const {
        auto actualNumServers = topologyDescription->getServers().size();
        auto expectedNumServers =
            bsonServers.getFieldNames<std::unordered_set<std::string>>().size();

        if (actualNumServers != expectedNumServers) {
            result.success = false;
            std::stringstream errorMessage;
            errorMessage << "expected " << expectedNumServers
                         << " server(s) in topology description. actual was " << actualNumServers
                         << ": ";
            for (const auto& server : topologyDescription->getServers()) {
                errorMessage << server->getAddress() << ", ";
            }
            result.errorDescriptions.push_back(std::make_pair("servers", errorMessage.str()));
        }

        for (const BSONElement& bsonExpectedServer : bsonServers) {
            const auto& serverAddress = bsonExpectedServer.fieldName();
            const auto& expectedServerDescriptionFields = bsonExpectedServer.Obj();

            const auto& serverDescription = topologyDescription->findServerByAddress(serverAddress);
            if (serverDescription) {
                for (const BSONElement& field : expectedServerDescriptionFields) {
                    validateServerField(*serverDescription, field, result);
                }
            } else {
                std::stringstream errorMessage;
                errorMessage << "could not find server '" << serverAddress
                             << "' in topology description.";
                auto errorDescription = std::make_pair("servers", errorMessage.str());
                result.errorDescriptions.push_back(errorDescription);
                result.success = false;
            }
        }
    }

    void validateTopologyDescription(const TopologyDescriptionPtr topologyDescription,
                                     const BSONObj bsonTopologyDescription,
                                     PhaseResult& result) const {
        {
            constexpr auto fieldName = "topologyType";
            auto expectedTopologyType = bsonTopologyDescription[fieldName].String();
            auto actualTopologyType = toString(topologyDescription->getType());
            if (expectedTopologyType != actualTopologyType) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(topologyDescriptionFieldName(fieldName),
                                   errorMessageNotEqual(expectedTopologyType, actualTopologyType));
                result.errorDescriptions.push_back(errorDescription);
            }
        }

        {
            constexpr auto fieldName = "setName";
            boost::optional<std::string> expectedSetName;
            auto actualSetName = topologyDescription->getSetName();
            auto bsonField = bsonTopologyDescription[fieldName];
            if (!bsonField.isNull()) {
                expectedSetName = bsonField.String();
            }
            if (expectedSetName != actualSetName) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(topologyDescriptionFieldName(fieldName),
                                   errorMessageNotEqual(expectedSetName, actualSetName));
                result.errorDescriptions.push_back(errorDescription);
            }
        }

        {
            constexpr auto fieldName = "logicalSessionTimeoutMinutes";
            boost::optional<int> expectedLSTM;
            auto actualLSTM = topologyDescription->getLogicalSessionTimeoutMinutes();
            auto bsonField = bsonTopologyDescription[fieldName];
            if (!bsonField.isNull()) {
                expectedLSTM = bsonField.numberInt();
            }
            if (expectedLSTM != actualLSTM) {
                result.success = false;
                auto errorDescription =
                    std::make_pair(topologyDescriptionFieldName(fieldName),
                                   errorMessageNotEqual(expectedLSTM, actualLSTM));
                result.errorDescriptions.push_back(errorDescription);
            }
        }

        {
            constexpr auto fieldName = "maxSetVersion";
            if (bsonTopologyDescription.hasField(fieldName)) {
                boost::optional<int> expectedMaxSetVersion;
                auto actualMaxSetVersion = topologyDescription->getMaxSetVersion();
                auto bsonField = bsonTopologyDescription[fieldName];
                if (!bsonField.isNull()) {
                    expectedMaxSetVersion = bsonField.numberInt();
                }
                if (expectedMaxSetVersion != actualMaxSetVersion) {
                    result.success = false;
                    auto errorDescription = std::make_pair(
                        topologyDescriptionFieldName(fieldName),
                        errorMessageNotEqual(expectedMaxSetVersion, actualMaxSetVersion));
                    result.errorDescriptions.push_back(errorDescription);
                }
            }
        }

        {
            constexpr auto fieldName = "maxElectionId";
            if (bsonTopologyDescription.hasField(fieldName)) {
                boost::optional<OID> expectedMaxElectionId;
                auto actualMaxElectionId = topologyDescription->getMaxElectionId();
                auto bsonField = bsonTopologyDescription[fieldName];
                if (!bsonField.isNull()) {
                    expectedMaxElectionId = bsonField.OID();
                }
                if (expectedMaxElectionId != actualMaxElectionId) {
                    result.success = false;
                    auto errorDescription = std::make_pair(
                        topologyDescriptionFieldName(fieldName),
                        errorMessageNotEqual(expectedMaxElectionId, actualMaxElectionId));
                    result.errorDescriptions.push_back(errorDescription);
                }
            }
        }

        {
            constexpr auto fieldName = "compatible";
            if (bsonTopologyDescription.hasField(fieldName)) {
                auto bsonField = bsonTopologyDescription[fieldName];
                bool expectedCompatible = bsonField.Bool();
                auto actualCompatible = topologyDescription->isWireVersionCompatible();
                if (expectedCompatible != actualCompatible) {
                    result.success = false;
                    auto errorDescription =
                        std::make_pair(topologyDescriptionFieldName(fieldName),
                                       errorMessageNotEqual(expectedCompatible, actualCompatible));
                    result.errorDescriptions.push_back(errorDescription);
                }
            }
        }
    }

    // the json tests don't actually use this value.
    constexpr static auto kLatency = mongo::Milliseconds(100);

    MongoURI _testUri;
    int _phaseNum;
    std::vector<IsMasterOutcome> _isMasterResponses;
    BSONObj _topologyOutcome;
};

// This class is responsible for parsing and executing a single json test file.
class JsonTestCase {
public:
    JsonTestCase(fs::path testFilePath) {
        parseTest(testFilePath);
    }

    struct TestCaseResult {
        bool success;
        std::vector<TestCasePhase::PhaseResult> phaseResults;
        std::string file;
        std::string name;
    };

    TestCaseResult execute() {
        std::unique_ptr<SdamConfiguration> config =
            std::make_unique<SdamConfiguration>(getSeedList(),
                                                _initialType,
                                                SdamConfiguration::kDefaultHeartbeatFrequencyMs,
                                                _replicaSetName);

        auto clockSource = std::make_unique<ClockSourceMock>();
        TopologyManager topology(*config, clockSource.get());

        TestCaseResult result{true, {}, _testFilePath, _testName};

        for (const auto& testPhase : _testPhases) {
            std::cout << banner("Phase " + std::to_string(testPhase.getPhaseNum()));
            auto phaseResult = testPhase.execute(topology);
            result.phaseResults.push_back(phaseResult);
            result.success = result.success && phaseResult.success;
            if (!result.success) {
                std::cout << "Phase " << phaseResult.phaseNumber << " failed." << std::endl;
                break;
            }
        }

        return result;
    }

    const std::string& Name() const {
        return _testName;
    }

private:
    void parseTest(fs::path testFilePath) {
        using namespace std;

        _testFilePath = testFilePath.string();

        {
            ifstream testFile(_testFilePath);
            ostringstream json;
            json << testFile.rdbuf();
            _jsonTest = fromjson(json.str());
        }

        _testName = _jsonTest.getStringField("description");
        _testUri = uassertStatusOK(mongo::MongoURI::parse(_jsonTest["uri"].String()));

        _replicaSetName = _testUri.getOption("replicaSet");
        if (!_replicaSetName) {
            if (_testUri.getServers().size() == 1) {
                _initialType = TopologyType::kSingle;
            } else {
                // We can technically choose either kUnknown or kSharded and be compliant,
                // but it seems that some of the json tests assume kUnknown as the initial state.
                // see: json_tests/sharded/normalize_uri_case.json
                _initialType = TopologyType::kUnknown;
            }
        } else {
            _initialType = TopologyType::kReplicaSetNoPrimary;
        }

        int phase = 0;
        const std::vector<BSONElement>& bsonPhases = _jsonTest["phases"].Array();
        for (auto bsonPhase : bsonPhases) {
            _testPhases.push_back(TestCasePhase(phase++, _testUri, bsonPhase.Obj()));
        }
    }

    std::vector<ServerAddress> getSeedList() {
        std::vector<ServerAddress> result;
        for (const auto& hostAndPort : _testUri.getServers()) {
            result.push_back(hostAndPort.toString());
        }
        return result;
    }

    BSONObj _jsonTest;
    std::string _testName;
    MongoURI _testUri;
    std::string _testFilePath;
    TopologyType _initialType;
    boost::optional<std::string> _replicaSetName;
    std::vector<TestCasePhase> _testPhases;
};

// This class runs (potentially) multiple json tests and reports their results.
class SdamJsonTestRunner {
public:
    SdamJsonTestRunner(std::string testDirectory, std::vector<std::string> testFilters)
        : _testFiles(scanTestFiles(testDirectory, testFilters)) {}

    std::vector<JsonTestCase::TestCaseResult> runTests() {
        std::vector<JsonTestCase::TestCaseResult> results;
        const auto testFiles = getTestFiles();
        for (auto jsonTest : testFiles) {
            auto testCase = JsonTestCase(jsonTest);
            try {
                std::cout << banner("Executing " + testCase.Name());
                results.push_back(testCase.execute());
            } catch (const DBException& ex) {
                std::stringstream error;
                error << "Exception while executing " << jsonTest.string() << ": " << ex.toString();
                std::string errorStr = error.str();
                results.push_back(JsonTestCase::TestCaseResult{
                    false,
                    {TestCasePhase::PhaseResult{false, {std::make_pair("exception", errorStr)}, 0}},
                    jsonTest.string(),
                    testCase.Name()});
                std::cerr << errorStr;
            }
        }
        return results;
    }

    int report(std::vector<JsonTestCase::TestCaseResult> results) {
        int numTestCases = results.size();
        int numSuccess = 0;
        int numFailed = 0;

        if (std::any_of(
                results.begin(), results.end(), [](const JsonTestCase::TestCaseResult& result) {
                    return !result.success;
                })) {
            std::cout << std::endl << banner("Failed Test Results");
        }

        for (const auto result : results) {
            auto file = result.file;
            auto testName = result.name;
            auto phaseResults = result.phaseResults;
            if (result.success) {
                ++numSuccess;
            } else {
                std::cout << banner(testName) << "error in file: " << file << std::endl;
                ++numFailed;
                const auto printError = [](const TestCasePhase::TestPhaseError& error) {
                    std::cout << "\t" << error.first << ": " << error.second << std::endl;
                };
                for (auto phaseResult : phaseResults) {
                    std::cout << "Phase " << phaseResult.phaseNumber << ": " << std::endl;
                    if (!phaseResult.success)
                        for (auto error : phaseResult.errorDescriptions)
                            printError(error);
                }
                std::cout << std::endl;
            }
        }
        std::cout << numTestCases << " test cases; " << numSuccess << " success; " << numFailed
                  << " failed." << std::endl;

        return numFailed;
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
    return testRunner.report(testRunner.runTests());
}
