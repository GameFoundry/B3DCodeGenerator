#pragma once
#include "B3DCommon.h"

/** Contains information about types we're generating code for, and mapping between native and script types. */
class TypeLookup
{
public:
	/** Returns a list of all files that need to be generated. */
	static const std::unordered_map<std::string, FileInfo>& GetFilesToGenerate() { return mFilesToGenerate; }

	/** Returns a list of all files that need to be generated. */
	static std::unordered_map<std::string, FileInfo>& GetFilesToGenerateMutable() { return mFilesToGenerate; }

	/** Retrieves an existing file information, or creates a new one if one doesn't exist with the provided file name. */
	static FileInfo& GetOrAddFileToGenerate(const std::string& name) { return mFilesToGenerate[name]; }

	/** Finds information about a struct with the provided name, if null if not found. */
	static StructInfo* FindStructInformation(const std::string& name);

	/** Finds information about a struct with the provided name, if null if not found. If @p isEditor is true, it will return the editor variant of the class, if two versions exist. */
	static ClassInfo* FindClassInformation(const std::string& name, bool isEditor);

	/** Finds information about an enum with the provided name, if null if not found. */
	static EnumInfo* FindEnumInformation(const std::string& name);

private:
	static std::unordered_map<std::string, FileInfo> mFilesToGenerate;
	
};