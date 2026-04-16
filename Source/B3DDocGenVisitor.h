//************************************ B3D Framework - Copyright 2026 Marko Pintera **************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DCommon.h"

#include <string>
#include <vector>

class CommentParser;
class JsonWriter;

/**
 * AST visitor used exclusively by the documentation-generation pass. Walks every class, struct, enum and free
 * function in the translation unit and emits a JSON entry for each. Buffers JSON snippets in memory and
 * flushes them all at once via WriteJSON().
 */
class BansheeDocGeneratorASTVisitor : public RecursiveASTVisitor<BansheeDocGeneratorASTVisitor>
{
public:
	BansheeDocGeneratorASTVisitor(ASTContext* astContext, CommentParser& commentParser)
		: mASTContext(astContext), mCommentParser(commentParser)
	{ }

	bool VisitCXXRecordDecl(CXXRecordDecl* recordDeclaration);
	bool VisitEnumDecl(EnumDecl* enumDeclaration);
	bool VisitFunctionDecl(FunctionDecl* functionDeclaration);

	/** Writes the buffered JSON to the provided file path. Returns true on success. */
	bool WriteJSON(StringRef outputPath);

private:
	void WriteClassObject(const CXXRecordDecl* recordDeclaration, JsonWriter& writer, bool isStruct);
	void WriteMethodMember(const CXXMethodDecl* methodDeclaration, StringRef parentQualifiedName, JsonWriter& writer);
	void WriteFieldMember(const FieldDecl* fieldDeclaration, StringRef parentQualifiedName, JsonWriter& writer);
	void WriteEnumObject(const EnumDecl* enumDeclaration, JsonWriter& writer);
	void WriteFunctionObject(const FunctionDecl* functionDeclaration, JsonWriter& writer);

	void WriteCommentEntry(const CommentEntry& entry, JsonWriter& writer);
	void WriteLocation(const Decl* decl, JsonWriter& writer);
	void WriteParamList(const FunctionDecl* decl, JsonWriter& writer);
	void WriteBaseList(const CXXRecordDecl* decl, JsonWriter& writer);
	void WriteTemplateParams(const TemplateParameterList* templateParameterList, JsonWriter& writer);

	std::string TypeToString(QualType type) const;
	std::string PrettyPrintSignature(const Decl* declaration) const;
	std::string ExpressionToString(const Expr* expression) const;
	std::string FlattenComment(const SmallVectorImpl<CommentParagraph>& paragraphs) const;
	std::string DetectCopydocTarget(const CommentEntry& entry) const;

	const char* VisibilityToString(AccessSpecifier accessSpecifier) const;
	bool IsInSystemHeader(const Decl* declaration) const;

	ASTContext* mASTContext;
	CommentParser& mCommentParser;

	std::vector<std::string> mClasses;
	std::vector<std::string> mMembers;
	std::vector<std::string> mEnums;
	std::vector<std::string> mFunctions;

	std::unordered_set<const Decl*> mEmittedDecls;
};
