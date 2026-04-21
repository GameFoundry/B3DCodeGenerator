//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once
#include "B3DCommon.h"
#include "B3DTypeLookup.h"

class CommentParser;

/** Offers various utility functionality useful for code generation. */
class GeneratorUtility
{
public:
	/** Ensures there is an empty folder at the provided path (either by deleting existing contents, or creating a new folder). */
	static void CleanAndPrepareFolder(const StringRef& folderPath);

	/** Retrieves a path that is relative to @p relativeTo path. */
	static std::string GetRelativePath(const StringRef& path, const StringRef& relativeTo);

	/**
	 * Creates an empty file.
	 *
	 * @param filename		Name of the file, with extension.
	 * @param outputFolder	Folder in which to place the file.
	 * @return				Stream to the newly created file.
	 */
	static std::ofstream CreateFile(const std::string& filename, StringRef outputFolder);
	
	/** Generates a copyright header. */
	static std::string GenerateCopyrightHeader(bool isEditor);

	/** Escapes special XML characters from the provided string and returns the escaped string. */
	static const std::string& EscapeXML(const std::string& data);

	/**
	 * Returns true if the provided type can be used as a return value from a C# method call.
	 *
	 * @param	typeInformation				Information about the native type to generate the interop type for.
	 * @param	typeMappingInformation		Mapping of the provided type in script.
	 */
	static bool CanBeReturned(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation);
};
