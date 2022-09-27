#pragma once
#include "B3DCommon.h"

class CommentParser;

/** Offers various utility functionality useful for parsing. */
class ParserUtility
{
public:
	/** Returns the namespace of the provided declaration. */
	static std::string GetNamespace(const NamedDecl* decl);

	/** Returns the fully qualified name (namespace + type) of the provided declaration. */
	static std::string GetFullName(const NamedDecl* decl);

	static void PostProcessFileInfos(CommentParser& commentParser);
private:
	/** Splits a method with default parameters into multiple methods, if some of the parameter default values cannot be parsed. */
	static void PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos);

	static void GatherIncludes(const std::string& typeName, int flags, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output);
	static void GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output);
	static void GatherIncludes(const StructInfo& structInfo, IncludesInfo& output);
};