#include "B3DGeneratorUtility.h"

void GeneratorUtility::CleanAndPrepareFolder(const StringRef& folderPath)
{
	if (sys::fs::exists(folderPath))
	{
		std::error_code ec;
		for (sys::fs::directory_iterator file(folderPath, ec), fileEnd; file != fileEnd && !ec; file.increment(ec))
			sys::fs::remove(file->path());
	}

	sys::fs::create_directories(folderPath);
}

std::string GeneratorUtility::GetRelativePath(const StringRef& path, const StringRef& relativeTo)
{
	SmallVector<char, 100> relativeToVector(relativeTo.begin(), relativeTo.end());

	vfs::getRealFileSystem()->makeAbsolute(relativeToVector);
	StringRef absRelativeTo(relativeToVector.data(), relativeToVector.size());

	SmallVector<char, 100> output;

	auto iterPath = sys::path::begin(path);
	auto iterRelativePath = sys::path::begin(absRelativeTo);

	bool foundRelative = false;
	for(; iterPath != sys::path::end(path) && iterRelativePath != sys::path::end(absRelativeTo); ++iterPath, ++iterRelativePath)
	{
		if (*iterPath != *iterRelativePath)
			break;

		foundRelative = true;
	}

	if (!foundRelative)
		return path.str();

	for(; iterRelativePath != sys::path::end(absRelativeTo); ++iterRelativePath)
		sys::path::append(output, "..");

	for (; iterPath != sys::path::end(path); ++iterPath)
		sys::path::append(output, *iterPath);

	sys::path::native(output, sys::path::Style::posix);
	return std::string(output.data(), output.size());
}

std::ofstream GeneratorUtility::CreateFile(const std::string& filename, StringRef outputFolder)
{
	std::string relativePath = "/" + filename;
	StringRef filenameRef(relativePath.data(), relativePath.size());

	SmallString<128> filepath = outputFolder;
	sys::path::append(filepath, filenameRef);

	std::ofstream output;
	output.open(filepath.str().str(), std::ios::out);

	return output;
}

std::string GeneratorUtility::GenerateCopyrightHeader(bool isEditor)
{
	std::stringstream output;
	if (isEditor)
		output << sEditorCopyrightNotice;
	else
		output << sFrameworkCopyrightNotice;

	return output.str();
}

const std::string& GeneratorUtility::EscapeXML(const std::string& data)
{
	std::string::size_type first = data.find_first_of("\"&<>", 0);
	if (first == std::string::npos)
		return data;

	static std::string buffer;
	buffer.reserve((size_t)(data.size() * 1.1f));
	buffer.clear();

	for (size_t pos = 0; pos != data.size(); ++pos)
	{
		switch (data[pos])
		{
		case '&':  buffer.append("&amp;");       break;
		case '\"': buffer.append("&quot;");      break;
		case '\'': buffer.append("&apos;");      break;
		case '<':  buffer.append("&lt;");        break;
		case '>':  buffer.append("&gt;");        break;
		default:   buffer.append(&data[pos], 1); break;
		}
	}

	return buffer;
}

bool GeneratorUtility::CanBeReturned(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	if (typeInformation.IsOutputParameter(typeMappingInformation))
		return false;

	if (typeInformation.IsArrayOrVector())
		return true;

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
		return false;

	return true;
}
