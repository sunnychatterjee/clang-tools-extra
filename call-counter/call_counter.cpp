#include "call_counter.h"
#include "clang/Frontend/CompilerInstance.h"
#include <fstream>
#include <iostream>
#include <Windows.h>

#define DEBUG 1

namespace
{
    constexpr auto MUTEX_NAME = L"Global\\CallCounter";
    constexpr auto OUTPUT_PATH_VAR_NAME = "CALLCOUNT_OUTPUT_PATH";

    using namespace CallCounter;

    // Reads "CAExcludePath" environment variable.
    // Note: This function is not cross platform.
    std::vector<std::string> GetCAExcludePath()
    {
        static std::vector<std::string> caExcludePath = {};
        static bool readExcludePath = false;
        if (!readExcludePath)
        {
            static auto constexpr CA_EXCLUDE_PATH = "CAExcludePath";
            char *value;
            size_t len;
            errno_t result = _dupenv_s(&value, &len, CA_EXCLUDE_PATH);
            if (!result && value)
            {
                char *pNext = value;
                do
                {
                    char *s = pNext;
                    if (*pNext == '\0')
                    {
                        break;
                    }
                    if (*pNext == ';')
                    {
                        pNext++;
                        continue;
                    }
                    pNext++;
                    while (*pNext != ';' && *pNext != '\0')
                    {
                        pNext++;
                    }
                    if (*pNext == ';')
                    {
                        *pNext++ = '\0';
                    }

                    // Ensure that the Filename is fully-qualified
                    // If we can't normalize it, ignore it.
                    char szFullPath[_MAX_PATH * 2];
                    if (!::GetFullPathNameA(s, _countof(szFullPath),
                        szFullPath, nullptr))
                        continue;

                    // Add the normalized path to the list of excluded paths
                    caExcludePath.emplace_back(szFullPath);
                } while (1);
            }

            free(value);
            readExcludePath = true;
        }

        return caExcludePath;
    }

    bool PrefixMatch(std::string_view prefix, std::string_view string)
    {
        auto res = std::mismatch(
            prefix.begin(), prefix.end(),
            string.begin(), string.end(),
            [](char ch1, char ch2) { return toupper(ch1) == toupper(ch2); }
        );

        return res.first == prefix.end();
    }

    // Checks if a file path is excluded from analysis.
    // Note: This function is not cross platform.
    bool IsPathExcluded(std::string_view path)
    {
        bool isExcluded = false;

        // Try to get full path for the given path.
        // If it fails, we'll just treat it as not excluded.
        char szFullPath[_MAX_PATH * 2];
        const auto caExcludedPath = GetCAExcludePath();
        if (caExcludedPath.size() > 0 &&
            ::GetFullPathNameA(path.data(), _countof(szFullPath), szFullPath, nullptr))
        {
            // Search the path against the list of excluded paths.
            // This should be a straightforward prefix match.
            const std::string fullPath = szFullPath;
            for (const auto& excludedPath : caExcludedPath)
            {
                if ((excludedPath.size() <= fullPath.size()) && PrefixMatch(excludedPath, fullPath))
                {
                    isExcluded = true;
                    break;
                }
            }
        }

        return isExcluded;
    }

    // Returns true if a function definition is to be excluded from analysis.
    // This is needed for supporting CAExcludePath environment variable.
    bool IsExcludedFromAnalysis(ASTContext *context, FunctionDecl *func)
    {
        const auto fullLocation = context->getFullLoc(func->getBeginLoc());
        if (!fullLocation.isValid())
            return true;
        const auto fileEntry = fullLocation.getFileEntry();
        if (!fileEntry)
            return true;
        if (IsPathExcluded(fileEntry->getName().str()))
            return true;

        return false;
    }

    // Returns true if the called method resides in some "class (anonymous namespace)"
    bool IsUnderAnonymousNameSpace(std::string_view typeName)
    {        
        constexpr static auto ANONYMOUS_NAMESPACE = "(anonymous namespace)::";
        return typeName.find(ANONYMOUS_NAMESPACE) != std::string::npos;
    }

    // Takes a fully qualified method name and removes all of the top-level
    // template arguments; we only care about the methods and the fundamental type.
    // e.g. std::vector<std::vector<int>>::back -> std::vector::back
    std::string RemoveTemplates(std::string_view name)
    {
        std::vector<char> buffer(name.size() + 1);
        int templDepth = 0;
        int next = 0;

        for (auto i = 0; i < name.size(); ++i)
        {
            auto c = name[i];

            if (c == '<')
            {
                ++templDepth;
                continue;
            }
            else if (c == '>')
            {
                --templDepth;
                continue;
            }
            else if (templDepth > 0)
            {
                continue;
            }

            buffer[next++] = c;
        }

        // null terminate the string
        buffer[next] = 0;
        return { buffer.data() };
    }

    // Gets the owning type name for a function call.
    // Returns true if type should be considered.
    // Returns false if type should be ignored.
    bool GetOwningTypeName(const CallInfo& info, std::string& owningTypeName)
    {
        if (info.memberCallDecl)
        {
            const auto methodDecl = info.memberCallDecl->getMethodDecl();
            const auto owningType = methodDecl->getThisType(*info.context)->getPointeeType();
            owningTypeName = owningType.getAsString();

            // Ignore method calls declared inside anonymous namespaces 
            if (IsUnderAnonymousNameSpace(owningTypeName))
                return false;

            // Strip off template arguments from the name
            owningTypeName = RemoveTemplates(owningTypeName);

            // At this point, the type names typically look like: "class MyNameSpace::CFoo".
            // We isolate the namespace by dropping the class name.
            auto pos = owningTypeName.find(" ");
            if (pos != std::string::npos)
                owningTypeName = owningTypeName.substr(pos + 1, owningTypeName.size() - pos);
        }
        else
        {
            owningTypeName = info.function->getQualifiedNameAsString();

            // Ignore free function calls declared inside anonymous namespaces
            if (IsUnderAnonymousNameSpace(owningTypeName))
                return false;

            // Strip off template arguments from the name
            owningTypeName = RemoveTemplates(owningTypeName);

            // Isolate the namespace by removing the function's name
            auto pos = owningTypeName.rfind("::");
            if (pos != std::string::npos)
                owningTypeName = owningTypeName.substr(0, pos);
            else
                owningTypeName = "::"; // No namespace was found so we should mark as global
        }

        return true;
    }

    // Generate report for collected call information
    // Output a formatted line to the output stream
    void OutputLine(std::ostream& out, const CallInfo& info)
    {      
        // Get owning type for function calls.
        // Ignore function calls in anonymous namespaces.
        std::string owningTypeName = "";
        if (!GetOwningTypeName(info, owningTypeName))
            return;

        std::string fileName = info.containingFile->getName();
        fileName = fileName.size() > 0 ? fileName : "none";
        const auto callName = RemoveTemplates(info.function->getNameInfo().getAsString());
        const auto containingFuncName = RemoveTemplates(info.containingFunc->getQualifiedNameAsString());

#if DEBUG
        llvm::outs() << fileName << ","
            << containingFuncName << ","
            << owningTypeName << ","
            << callName << ","
            << info.line << ","
            << (info.usedInBranch ? "1" : "0") << ","
            << (info.usedInLoop ? "1" : "0");

        llvm::outs() << "\n";
#endif // DEBUG

        out << fileName << ","
            << containingFuncName << ","
            << owningTypeName << ","
            << callName << ","
            << info.line << ","
            << (info.usedInBranch ? "1" : "0") << ","
            << (info.usedInLoop ? "1" : "0")
            << std::endl;
    }

    // Note: The synchronization code is not cross platform
    void WriteOutput(const std::vector<CallInfo>& calls)
    {
        constexpr size_t bufferSize = 512;
        size_t sizeRead;
        char outPath[bufferSize]{ 0 };

        const auto err = getenv_s(&sizeRead, outPath, bufferSize, OUTPUT_PATH_VAR_NAME);

        if (err || sizeRead == 0)
            return;

        auto resultsPath = std::string(outPath);

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = nullptr;
        sa.bInheritHandle = true;
        auto mu = CreateMutex(&sa, false, MUTEX_NAME);

        if (!mu)
            return;

        WaitForSingleObject(mu, INFINITE);

        try
        {
            std::ofstream file(resultsPath, std::ios::app);
            for (const auto call : calls)
                OutputLine(file, call);
        }
        catch (...)
        {
            std::cout << "Error writing method call results." << std::endl;
        }
        ReleaseMutex(mu);
    }
} // anonymous namespace

namespace CallCounter
{
    using namespace clang;

    FindFunctionDefinitionVisitor::FindFunctionDefinitionVisitor(ASTContext *context)
        : context_(context)
    {
    }

    bool FindFunctionDefinitionVisitor::VisitFunctionDecl(FunctionDecl *func)
    {
        if (!context_)
            return false;

        if (func && func->hasBody() && !IsExcludedFromAnalysis(context_, func))
        {
            currentFunction_ = func;
            WalkStmt(func->getBody(), false);
            WriteOutput(callInfo_);
            callInfo_.clear();
        }
        return true;
    }

    void FindFunctionDefinitionVisitor::WalkStmt(Stmt *stmt, bool insideIfCondition)
    {
        if (!stmt)
            return;

        // Track calls within if-statements
        if (const auto ifStmt = dyn_cast<IfStmt> (stmt))
        {
            WalkIfConditionExpr(ifStmt->getCond());
            WalkStmt(ifStmt->getThen(), insideIfCondition);
            WalkStmt(ifStmt->getElse(), insideIfCondition);
            return;
        }

        // Track calls
        if (const auto callExpr = dyn_cast<CallExpr> (stmt))
            WalkCallExpr(callExpr, insideIfCondition);

        // Track children
        for (const auto child : stmt->children())
            WalkStmt(child, insideIfCondition);
    }

    void FindFunctionDefinitionVisitor::WalkIfConditionExpr(Expr *expr)
    {
        if (!expr)
            return;

        if (const auto callExpr = dyn_cast<CallExpr> (expr))
            WalkCallExpr(callExpr, true);

        for (const auto childExpr : expr->children())
            WalkStmt(childExpr, true);
    }

    void FindFunctionDefinitionVisitor::WalkCallExpr(CallExpr *callExpression, bool insideIfCondition)
    {
        assert(context_);
        assert(callExpression);

        // Make sure that the call expression has a valid non-null type
        const auto callType = callExpression->getType();
        if (!callType.getTypePtrOrNull())
            return;

        // Make sure the location of the call expression is valid
        const auto fullLocation =
            context_->getFullLoc(callExpression->getBeginLoc());
        if (!fullLocation.isValid())
            return;
        const auto fileEntry = fullLocation.getFileEntry();
        if (!fileEntry)
            return;

        // We track direct calls for now
        const auto calledFunc = callExpression->getDirectCallee();
        if (!calledFunc)
            return;

        callInfo_.emplace_back(
            CallInfo{ fileEntry, currentFunction_, dyn_cast<CXXMemberCallExpr>(callExpression), context_, calledFunc, fullLocation.getSpellingLineNumber(), insideIfCondition }
        );
    }

    FindFunctionDefinitionConsumer::FindFunctionDefinitionConsumer(ASTContext *context)
        : visitor_(context)
    {
    }

    void FindFunctionDefinitionConsumer::HandleTranslationUnit(clang::ASTContext &context)
    {
        visitor_.TraverseDecl(context.getTranslationUnitDecl());
    }

    std::unique_ptr<clang::ASTConsumer> FindFunctionDefinitionAction::CreateASTConsumer(
        clang::CompilerInstance &compiler, llvm::StringRef inFile
    )
    {
        return std::unique_ptr<clang::ASTConsumer>(
            new FindFunctionDefinitionConsumer(&compiler.getASTContext())
            );
    }
} // namespace CallCounter
