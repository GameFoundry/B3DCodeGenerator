//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
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

	/** Checks is the provided declaration a Banshee::Module type. */
	static bool CheckIsBuiltinModuleType(const CXXRecordDecl* decl);

	/** Returns true if the provided declaration is one of the builtin base types (e.g. component, resource, IReflectable). */
	static bool IsBuiltinBaseType(const CXXRecordDecl* decl);

	/** Checks if the provided type has IScriptExportable base class. */
	static bool HasIScriptExportableBaseClass(const CXXRecordDecl* decl);

	/** Converts the provided name from camel case or screaming snake case into pascal case. */
	static std::string ConvertToPascalCase(const std::string& name);

	static bool MapBuiltinPrimitiveTypeToCppType(BuiltinType::Kind kind, std::string& output);
	static ApiFlags ParseAPIFromExportFlags(int exportFlags);
};
