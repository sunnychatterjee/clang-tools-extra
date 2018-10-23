//===---- tools/extra/call_counter_main.cpp ----===//
//
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#include "call_counter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

using namespace llvm;
using namespace clang::tooling;

// Set up the command line options
static cl::extrahelp common_help(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp("\nThis is a clang based tool which collects information about called functions in a C++ program.\n");
static cl::OptionCategory call_counter_category("call-counter options");

int main(int argc, const char **argv)
{
    CommonOptionsParser optionsParser(argc, argv, call_counter_category);

    ClangTool tool(
        optionsParser.getCompilations(),    // Causes annoying warning, we have to use -- on command line
        optionsParser.getSourcePathList()
    );

    int result = tool.run(newFrontendActionFactory<CallCounter::FindFunctionDefinitionAction>().get());
    return result;
}
