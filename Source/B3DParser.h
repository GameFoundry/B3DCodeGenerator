#pragma once

#include "B3DCommon.h"
#include "B3DCommentParser.h"

struct FunctionTypeInfo;

class BansheeCodeGeneratorASTVisitor : public RecursiveASTVisitor<BansheeCodeGeneratorASTVisitor>
{
public:
	explicit BansheeCodeGeneratorASTVisitor(CompilerInstance* CI, CommentParser& commentParser);

	bool VisitEnumDecl(EnumDecl* decl);
	bool VisitCXXRecordDecl(CXXRecordDecl* decl);

private:
	bool evaluateLiteral(Expr* expr, std::string& evalValue);
	bool evaluateExpression(Expr* expr, std::string& evalValue, std::string& valType);
	bool parseEventSignature(QualType type, FunctionTypeInfo& typeInfo, bool& isCallback);
	bool parseEvent(ValueDecl* decl, const std::string& className, MethodInfo& eventInfo);
	bool parseType(QualType type, VarTypeInfo& outType, bool returnValue = false);
	std::string parseTemplArguments(const std::string& className, const TemplateArgument* tmplArgs, unsigned numArgs, SmallVector<TemplateParamInfo, 0>* templParams);

	ASTContext* astContext;
	Preprocessor& preprocessor;
	CommentParser& mCommentParser;
};
