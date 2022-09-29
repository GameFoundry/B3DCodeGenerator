#pragma once
#include "B3DCommon.h"

struct FunctionTypeInfo;

/** Handles parsing of JavaDoc comments. */
class CommentParser
{
public:
	CommentParser() = default;

	/** Assigns the AST context to use for comment parsing. Must be set before performing any comment parsing actions. */
	void SetASTContext(ASTContext& astContext) { mASTContext = &astContext; }

	/**
	 * Parses the comments for the provided declaration.
	 *
	 * @param decl			Declaration to parse.
	 * @param entry			Parsed comments, if successful.
	 * @return				True if comment parsing was successful.
	 */
	bool ParseComments(const Decl* decl, CommentEntry& entry);

	/** Parses all comments for a class or a struct, and all of its methods and fields. Resulting information is registered in the global comment lookup table. */
	void ParseAndRegisterAllComments(const CXXRecordDecl* decl);

	/** Parses all comments for an enum and it's elements. Resulting information is registered in the global comment lookup table. */
	void ParseAndRegisterAllComments(const EnumDecl* decl);

	/**
	 * Attempts to lookup the comment matching the provided string.
	 *
	 * @param	value				String that determines the type, method or field we're looking up.
	 * @param	parentType			Type in which we're performing the lookup in. Determines the context.
	 * @param	currentNamespace	Namespace we're performing the lookup from. Determines the context.
	 * @param	outputComment		Found comment, if any.
	 * @return						True if the lookup was successful.
	 */
	bool TryLookupComment(const std::string& value, const std::string& parentType, const SmallVector<std::string, 4>& currentNamespace, CommentEntry& outputComment);

	/**
	 * Scans the provided comment for any copydoc commands, and replaces them with actual comments from entries referenced by the commands.
	 *
	 * @param[in,out]	comment				Comment to scan for copydoc commands, and to modify with resolved comments.
	 * @param			parentType			Type for which the provided comment is for.
	 * @param			currentNamespace	Namespace of the type the comment is located in.
	 */
	void ResolveCopydocComments(CommentEntry& comment, const std::string& parentType, const SmallVector<std::string, 4>& currentNamespace);

	/** Checks if any of the parameters referenced in the comment text don't exist in the actual parameter list, and if so, converts them to generic declaration references. */
	static void EnsureValidParameterReferenceComments(const std::vector<VariableInformation>& paramInfos, CommentText& comment);

	/** Checks if any of the parameters referenced in the comment don't exist in the actual parameter list, and if so, converts them to generic declaration references. */
	static void EnsureValidParameterReferenceComments(const std::vector<VariableInformation>& paramInfos, CommentEntry& comment);

	/** Clears any parameter references from the provided comment entry, and converts them to generic declaration references. */
	static void ClearParameterReferenceComments(CommentEntry& comment);

	// TODO - Move to XMLCommentGenerator class
	static std::string GenerateXMLCommentText(const CommentText& commentTextEntry);
	static std::string GenerateXMLCommentText(const SmallVector<CommentText, 2>& commentTextEntries);
	static std::string GenerateXMLComments(const CommentEntry& commentEntry, const std::string& indent);
private:
	/** Parses information about the provided declaration, including type name and namespace. Performs additional parameter parsing in case this is a function declaration. */
	void ParseCommentInfo(const NamedDecl* decl, CommentInformation& commentInfo);

	/** Parses information about the provided function declaration and appends it to the provided @p commentInfo overload list. The function declaration information includes the types of all parameters. */
	void ParseCommentMethodInfo(const FunctionDecl* decl, CommentInformation& commentInfo) const;

	/** Looks up existing comments for the provided declaration, and if not found, parses the comments and registers them in the global lookup table. */
	void LookupOrParseComments(const NamedDecl* decl, CommentInformation& commentInfo);

	ASTContext* mASTContext = nullptr;

	// Comment lookup tables, used primarily for copydoc resolve
	std::vector<CommentInformation> mCommentTypeInformation;
	std::unordered_map<std::string, int> mCommentLookupViaFullName;
	std::unordered_map<std::string, SmallVector<int, 2>> mCommentLookupViaTypeName;
};
