#include <fstream>
#include <iostream>
#include <stdexcept>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/schema.h>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

}

int main(int argc, char** argv)
{
    try {
        require(argc == 3, "Expected schema and scan report paths");
        std::ifstream schemaFile(argv[1], std::ios::binary);
        require(schemaFile.is_open(), "Could not open scan report JSON Schema");
        rapidjson::IStreamWrapper schemaStream(schemaFile);
        rapidjson::Document schemaDocument;
        schemaDocument.ParseStream(schemaStream);
        require(!schemaDocument.HasParseError(), "Scan report JSON Schema is not valid JSON");

        std::ifstream reportFile(argv[2], std::ios::binary);
        require(reportFile.is_open(), "Could not open generated scan report");
        rapidjson::IStreamWrapper reportStream(reportFile);
        rapidjson::Document reportDocument;
        reportDocument.ParseStream(reportStream);
        require(!reportDocument.HasParseError(), "Generated scan report is not valid JSON");

        rapidjson::SchemaDocument schema(schemaDocument);
        rapidjson::SchemaValidator validator(schema);
        require(reportDocument.Accept(validator),
            "Generated scan report does not conform to the scan report JSON Schema");
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
