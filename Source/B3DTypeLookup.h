#pragma once
#include "B3DCommon.h"

class CommentParser;

/** Contains information about types we're generating code for, and mapping between native and script types. */
class TypeLookup
{
public:

	/** Registers a new class to be generated in the specified file. */
	static void RegisterEntryToGenerate(const std::string& fileName, ClassInfo classInfo);

	/** Registers a new struct to be generated in the specified file. */
	static void RegisterEntryToGenerate(const std::string& fileName, StructInfo structInfo);

	/** Registers a new enum to be generated in the specified file. */
	static void RegisterEntryToGenerate(const std::string& fileName, EnumInfo enumInfo);

	/** Finds information about a struct with the provided name, if null if not found. */
	static StructInfo* FindStructInformation(const std::string& typeName);

	/** Finds information about a struct with the provided name, if null if not found. If @p isEditor is true, it will return the editor variant of the class, if two versions exist. */
	static ClassInfo* FindClassInformation(const std::string& typeName, bool isEditor);

	/** Finds information about an enum with the provided name, if null if not found. */
	static EnumInfo* FindEnumInformation(const std::string& typeName);

	/** Finds information about a struct with the provided name, if null if not found. Only searches the specified file. */
	static StructInfo* FindStructInformationInFile(const std::string& fileName, const std::string& typeName);

	/** Finds information about a struct with the provided name, if null if not found. Only searches the specified file. */
	static ClassInfo* FindClassInformationInFile(const std::string& fileName, const std::string& typeName);

	/** Finds information about an enum with the provided name, if null if not found. Only searches the specified file. */
	static EnumInfo* FindEnumInformationInFile(const std::string& fileName, const std::string& typeName);

	/** Returns a list of all files that need to be generated. */
	static const std::unordered_map<std::string, FileInfo>& GetFilesToGenerate() { return mFilesToGenerate; }

	/**
	 * Registers a mapping between a native and script type. This mapping will be used whenever the native type is encountered during code generation.
	 *
	 * @param nameSpace				Namespace of the native type.
	 * @param nativeName			Type name of the native type.
	 * @param nativeFilePath		Path to the file in which the native type is declared in.
	 * @param scriptName			Type name of the script type the native type maps to.
	 * @param scriptFileName		Name of the file (without extension) in which the script type will be generated in.
	 * @param api					API to export the script type into.
	 * @param typeCategory			Type category that describes the type being mapped.
	 * @param enumUnderlyingType	Underlying storage type, if the type category is an enum.
	 */
	static void RegisterNativeToScriptTypeMapping(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptFileName, ApiFlags api, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType = BuiltinType::NullPtr);

	/** Same as RegisterNativeToScriptTypeMapping, except the script file is provided as an explicit path rather than a file name. Useful for types that have custom interop wrappers. */
	static void RegisterNativeToScriptTypeMappingWithExplicitPath(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptFilePath, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType = BuiltinType::NullPtr);

	/** Returns the information about a native type maps to a script type. */
	static TypeMappingInformation GetNativeToScriptTypeMapping(const std::string& typeName);

	/**
	 * Returns the information about how a native type maps to a script type. The provided type information supports extra information about how the type
	 * is being used (e.g. passed as a pointer, reference, resource handle, array etc.), and will utilize this information to return the underlying type.
	 */
	static TypeMappingInformation GetNativeToScriptTypeMapping(const VariableTypeInformation& typeInformation);

	/**
	 * Returns the name of the script interop type used for the provided type name
	 *
	 * @param typeName					Type name of the type to look up.
	 * @param isResourceReference	If the type is a resource, this will return a resource reference script interop class, rather than the resource's own interop class.
	 * @return						Name of the type used for script interop for the provided type name.
	 */
	static std::string GetScriptInteropTypeName(const std::string& typeName, bool isResourceReference = false);

	/**
	 * Performs final post processing on all files to generate. Should be called once after all files to generate have been registered, before actually using the types for code generation.
	 *
	 * @param commentParser			Contains lookup for all comments registered during parsing.
	 */
	static void FinalizeFilesToGenerate(CommentParser& commentParser);

	/**
	 * Finds derived classes of the provided class.
	 *
	 * @param typeName			Type name of the class to do the lookup for.
	 * @param output			A list of all derived classes.
	 * @param onlyDirect		If true, only the direct children will be returned, and if false, all derived classes will be returned.
	 */
	static void GetDerivedClasses(const std::string& typeName, std::vector<std::string>& output, bool onlyDirect = false);

	/**
	 * Registers a new external method for the provided type.
	 *
	 * @param typeName			Type for which we're providing the extension.
	 * @param methodInfo		Information about the extension method.
	 */
	static void RegisterExternalMethod(const std::string& typeName, const MethodInfo& methodInfo);
private:
	/** Information about which classes derive from a base class. */
	struct BaseClassInfo
	{
		std::vector<std::string> ChildClasses;
	};

	/** Maps a C++ primitive type such as int uint32_t or int8_t, to C# type. */
	static std::string MapCppPrimitiveTypeToCSharpType(const std::string& cppType);

	/** Splits a method with default parameters into multiple methods, if some of the parameter default values cannot be parsed. */
	static void PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos);

	/**
	 * Gathers includes required for the specified type.
	 *
	 * @param	typeInformation		Information about the type.
	 * @param	isEditor			True if the type is part of editor API.
	 * @param	output				List of includes and forward declares required for the declaration. This will be appended with any new includes/forward declares.
	 */
	static void GatherIncludes(const VariableTypeInformation& typeInformation, bool isEditor, IncludesInfo& output);

	/**
	 * Gathers includes required for the specified method.
	 *
	 * @param	methodInfo			Information about the type.
	 * @param	isEditor			True if the method is part of editor API.
	 * @param	output				List of includes and forward declares required for the declaration. This will be appended with any new includes/forward declares.
	 */
	static void GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output);

	/**
	 * Gathers includes required for the specified field.
	 *
	 * @param	fieldInfo			Information about the field.
	 * @param	isEditor			True if the field is part of editor API.
	 * @param	output				List of includes and forward declares required for the declaration. This will be appended with any new includes/forward declares.
	 */
	static void GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output);

	/**
	 * Gathers includes required for the specified class.
	 *
	 * @param	classInfo			Information about the class.
	 * @param	output				List of includes and forward declares required for the declaration. This will be appended with any new includes/forward declares.
	 */
	static void GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output);

	/**
	 * Gathers includes required for the specified struct.
	 *
	 * @param	structInfo			Information about the struct.
	 * @param	output				List of includes and forward declares required for the declaration. This will be appended with any new includes/forward declares.
	 */
	static void GatherIncludes(const StructInfo& structInfo, IncludesInfo& output);

	/** Contains a map of native types to script types. The key is the native name as provided in ClassInfo.Name, StructInfo.Name or EnumInfo.Name. */
	static std::unordered_map<std::string, FileInfo> mFilesToGenerate;
	static std::unordered_map<std::string, TypeMappingInformation> mNativeToScriptTypeMap;
	static std::unordered_map<std::string, ExternalClassInfos> mExternalClassInfos;
	static std::unordered_map<std::string, BaseClassInfo> mBaseClassLookup;


	
};