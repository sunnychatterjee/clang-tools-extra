#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendActions.h"

using namespace clang;

namespace CallCounter
{
    // Used for tracking call infromation during function analysis
    struct CallInfo
    {
        const FileEntry *containingFile = nullptr; // Source file name in which the function call appears.
        const FunctionDecl *containingFunc = nullptr; // Function definition in which the call appears.
        const CXXMemberCallExpr *memberCallDecl = nullptr; // Member call declaration. Stores nullptr for non-member calls.
        ASTContext *context = nullptr; // AST context, needed for inferring owning type for member calls.
        const FunctionDecl *function = nullptr; // Function declaration of the current call.
        const unsigned line = 0; // Line number in which the call appears.
        const bool usedInBranch = false; // True if call appears inside an if condition block. False otherwise.
        const bool usedInLoop = false; // True if call appears inside a loop condition. False otherwise. Not implemented.
    };

    class FindFunctionDefinitionVisitor : public RecursiveASTVisitor<FindFunctionDefinitionVisitor>
    {
    public:
        explicit FindFunctionDefinitionVisitor(ASTContext *context);
        bool VisitFunctionDecl(FunctionDecl *func);

    private:
        void WalkStmt(Stmt *stmt, bool insideIfCondition);
        void WalkIfConditionExpr(Expr *expr);
        void WalkCallExpr(CallExpr *callExpression, bool insideIfCondition);

    private:
        ASTContext *context_ = nullptr;
        FunctionDecl *currentFunction_ = nullptr;
        std::vector<CallInfo> callInfo_;
    };


    class FindFunctionDefinitionConsumer : public clang::ASTConsumer
    {
    public:
        explicit FindFunctionDefinitionConsumer(ASTContext *context);
        virtual void HandleTranslationUnit(clang::ASTContext &Context);

    private:
        FindFunctionDefinitionVisitor visitor_;
    };


    class FindFunctionDefinitionAction : public clang::ASTFrontendAction
    {
    public:
        FindFunctionDefinitionAction() = default;
        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &compiler, llvm::StringRef inFile
        );
    };
} // namespace CallCounter
