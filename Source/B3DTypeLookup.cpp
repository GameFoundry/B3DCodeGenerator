#include "B3DTypeLookup.h"

MethodInfo ClassInfo::FindUnusedConstructorSignature() const
{
	auto checkSignature = [](int numParams, const MethodInfo& info)
	{
		if ((int)info.Parameters.size() != numParams)
			return true;

		for (auto& paramInfo : info.Parameters)
		{
			if (paramInfo.TypeInformation.TypeName != "bool")
				return true;
		}

		return false;
	};

	int numBools = 1;
	while (true)
	{
		bool isSignatureValid = true;

		// Check normal constructors
		for (auto& entry : Constructors)
		{
			if(!checkSignature(numBools, entry))
			{
				isSignatureValid = false;
				break;
			}
		}

		// Check external constructors
		if(isSignatureValid)
		{
			for (auto& entry : Methods)
			{
				bool isConstructor = entry.IsFlagSet(MethodFlags::Constructor);
				if (!isConstructor)
					continue;

				if(!checkSignature(numBools, entry))
				{
					isSignatureValid = false;
					break;
				}
			}
		}

		if (isSignatureValid)
			break;

		numBools++;
	}

	MethodInfo output;
	output.NativeName = NativeNameWithoutTemplateArguments;
	output.ScriptName = NativeNameWithoutTemplateArguments;
	output.MethodFlags = (int)MethodFlags::Constructor;
	output.Visibility = CSVisibility::Private;

	for (int i = 0; i < numBools; i++)
	{
		VariableInformation paramInfo;
		paramInfo.Name = "__dummy" + std::to_string(i);
		paramInfo.TypeInformation.TypeName = "bool";
		paramInfo.TypeInformation.TypeCategory = VariableTypeCategory::Primitive;

		output.Parameters.push_back(paramInfo);
	}

	return output;
}

std::unordered_map<std::string, FileInfo> TypeLookup::mFilesToGenerate;
std::unordered_map<std::string, TypeMappingInformation> TypeLookup::mNativeToScriptTypeMap;

StructInfo* TypeLookup::FindStructInformation(const std::string& typeName)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& structInfo : fileInfo.second.Structs)
		{
			if (structInfo.NativeName == typeName)
				return &structInfo;
		}
	}

	return nullptr;
}

ClassInfo* TypeLookup::FindClassInformation(const std::string& typeName, bool isEditor)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			if (classInfo.NativeName != typeName)
				continue;

			// Two versions of editor and Framework class migth exist, make sure to pick the right one
			if((isEditor && classInfo.API == ApiFlags::Framework) || (!isEditor &&  IsAPIEditor(classInfo.API)))
				continue;

			return &classInfo;
		}
	}

	return nullptr;
}

EnumInfo* TypeLookup::FindEnumInformation(const std::string& typeName)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& enumInfo : fileInfo.second.Enums)
		{
			if (enumInfo.NativeName == typeName)
				return &enumInfo;
		}
	}

	return nullptr;
}

StructInfo* TypeLookup::FindStructInformationInFile(const std::string& fileName, const std::string& typeName)
{
	auto found = mFilesToGenerate.find(fileName);
	if (found == mFilesToGenerate.end())
		return nullptr;

	for (auto& structInfo : found->second.Structs)
	{
		if (structInfo.NativeName == typeName)
			return &structInfo;
	}

	return nullptr;
}

ClassInfo* TypeLookup::FindClassInformationInFile(const std::string& fileName, const std::string& typeName)
{
	auto found = mFilesToGenerate.find(fileName);
	if (found == mFilesToGenerate.end())
		return nullptr;

	for (auto& classInfo : found->second.Classes)
	{
		if (classInfo.NativeName == typeName)
			return &classInfo;
	}

	return nullptr;
}

EnumInfo* TypeLookup::FindEnumInformationInFile(const std::string& fileName, const std::string& typeName)
{
	auto found = mFilesToGenerate.find(fileName);
	if (found == mFilesToGenerate.end())
		return nullptr;

	for (auto& enumInfo : found->second.Enums)
	{
		if (enumInfo.NativeName == typeName)
			return &enumInfo;
	}

	return nullptr;
}

void TypeLookup::RegisterEntryToGenerate(const std::string& fileName, StructInfo structInfo)
{
	FileInfo& fileInfo = mFilesToGenerate[fileName];

	if (IsAPIEditor(structInfo.API))
	{
		// Editor only file
		if(!IsAPIFramework(structInfo.API))
		{
			fileInfo.InEditor = true;
			fileInfo.Structs.push_back(structInfo);
		}
		else // Editor and framework, add new file for editor
		{
			structInfo.API = ApiFlags::Framework;
			fileInfo.Structs.push_back(structInfo);

			structInfo.API = ApiFlags::Editor;

			const std::string editorFile = fileName + ".editor";

			FileInfo& editorFileInfo = mFilesToGenerate[editorFile];
			editorFileInfo.InEditor = true;

			editorFileInfo.Structs.push_back(structInfo);
		}
	}
	else // Non-editor, framework or engine
	{
		fileInfo.Structs.push_back(structInfo);
	}
}

void TypeLookup::RegisterEntryToGenerate(const std::string& fileName, ClassInfo classInfo)
{
	FileInfo& fileInfo = mFilesToGenerate[fileName];

	if (IsAPIEditor(classInfo.API))
	{
		// Editor only file
		if(!IsAPIFramework(classInfo.API))
		{
			fileInfo.InEditor = true;
			fileInfo.Classes.push_back(classInfo);
		}
		else // Editor and framework, add new file for editor
		{
			classInfo.API = ApiFlags::Framework;
			fileInfo.Classes.push_back(classInfo);

			classInfo.API = ApiFlags::Editor;

			const std::string editorFile = fileName + ".editor";

			FileInfo& editorFileInfo = mFilesToGenerate[editorFile];
			editorFileInfo.InEditor = true;

			editorFileInfo.Classes.push_back(classInfo);
		}
	}
	else // Non-editor, framework or engine
	{
		fileInfo.Classes.push_back(classInfo);
	}
}

void TypeLookup::RegisterEntryToGenerate(const std::string& fileName, EnumInfo enumInfo)
{
	FileInfo& fileInfo = mFilesToGenerate[fileName];

	if (IsAPIEditor(enumInfo.API))
	{
		// Editor only file
		if(!IsAPIFramework(enumInfo.API))
		{
			fileInfo.InEditor = true;
			fileInfo.Enums.push_back(enumInfo);
		}
		else // Editor and framework, add new file for editor
		{
			enumInfo.API = ApiFlags::Framework;
			fileInfo.Enums.push_back(enumInfo);

			enumInfo.API = ApiFlags::Editor;

			const std::string editorFile = fileName + ".editor";

			FileInfo& editorFileInfo = mFilesToGenerate[editorFile];
			editorFileInfo.InEditor = true;

			editorFileInfo.Enums.push_back(enumInfo);
		}
	}
	else // Non-editor, framework or engine
	{
		fileInfo.Enums.push_back(enumInfo);
	}
}

void TypeLookup::RegisterNativeToScriptTypeMapping(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptFileName, ApiFlags api, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType)
{
	const std::string destinationFile = "BsScript" + scriptFileName + ".generated.h";
	std::string destinationFileEditor = destinationFile;

	// Going to need separate file for editor?
	if (IsAPIEditor(api) && IsAPIFramework(api))
		destinationFileEditor = "BsScript" + scriptFileName + ".editor.generated.h";

	TypeMappingInformation typeMappingInformation = TypeMappingInformation(nameSpace, scriptName, typeCategory, nativeFilePath, destinationFile, destinationFileEditor);
	typeMappingInformation.EnumUnderlyingType = enumUnderlyingType;

	mNativeToScriptTypeMap[nativeName] = typeMappingInformation;
}

void TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptFilePath, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType)
{
	TypeMappingInformation typeMappingInformation = TypeMappingInformation(nameSpace, scriptName, typeCategory, nativeFilePath, scriptFilePath);
	typeMappingInformation.EnumUnderlyingType = enumUnderlyingType;

	mNativeToScriptTypeMap[nativeName] = typeMappingInformation;
}

TypeMappingInformation TypeLookup::GetNativeToScriptTypeMapping(const std::string& typeName)
{
	auto iterFind = mNativeToScriptTypeMap.find(typeName);
	if (iterFind == mNativeToScriptTypeMap.end())
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = MapCppPrimitiveTypeToCSharpType(typeName);
		outType.TypeCategory = ExportedClassTypeCategory::Primitive;

		errs() << "Unable to map type \"" << typeName << "\". Assuming same name as source.\n";
		return outType;
	}
	
	return iterFind->second;
}

TypeMappingInformation TypeLookup::GetNativeToScriptTypeMapping(const VariableTypeInformation& typeInformation)
{
	switch (typeInformation.TypeCategory)
	{
	case VariableTypeCategory::Primitive:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = MapCppPrimitiveTypeToCSharpType(typeInformation.TypeName);
		outType.TypeCategory = ExportedClassTypeCategory::Primitive;

		return outType;
	}
	case VariableTypeCategory::String:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ExportedClassTypeCategory::String;

		return outType;
	}
	case VariableTypeCategory::WString:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ExportedClassTypeCategory::WString;

		return outType;
	}
	case VariableTypeCategory::Path:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ExportedClassTypeCategory::Path;

		return outType;
	}
	case VariableTypeCategory::MonoObject:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "object";
		outType.TypeCategory = ExportedClassTypeCategory::MonoObject;

		return outType;
	}
	case VariableTypeCategory::AsyncOp:
	{
		TypeMappingInformation underlyingTypeMapping;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ExportedClassTypeCategory::Class;
		}
		else
		{
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		TypeMappingInformation outType = underlyingTypeMapping;
		outType.ScriptTypeName = "AsyncOp<" + underlyingTypeMapping.ScriptTypeName + ">";

		return outType;
	}
	case VariableTypeCategory::ResourceHandle:
	{
		TypeMappingInformation underlyingTypeMapping;
		VariableTypeInformation underlyingType;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ExportedClassTypeCategory::Class;

			underlyingType.TypeName = "Unknown";
			underlyingType.TypeCategory = VariableTypeCategory::General;
		}
		else
		{
			underlyingType = *typeInformation.UnderlyingType;
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		if(typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
		{
			TypeMappingInformation outType = underlyingTypeMapping;

			if (underlyingType.TypeName == "Resource")
				outType.ScriptTypeName = "RRefBase";
			else
				outType.ScriptTypeName = "RRef<" + underlyingTypeMapping.ScriptTypeName + ">";

			return outType;
		}
		else
			return underlyingTypeMapping;
	}
	// Just forward the type resolve to the underlying type. Note we don't support nested vectors, arrays or shared pointers
	case VariableTypeCategory::Vector:
	case VariableTypeCategory::SmallVector:
	case VariableTypeCategory::Array:
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::ComponentOrActor:
	case VariableTypeCategory::Flags:
	case VariableTypeCategory::SharedPointer:
	{
		TypeMappingInformation underlyingTypeMapping;
		if (!typeInformation.UnderlyingType)
		{
			errs() << "Unable to map underlying type for \"" << typeInformation.TypeName << "\". No underlying type found. \n";

			underlyingTypeMapping.ScriptTypeName = "Unknown";
			underlyingTypeMapping.TypeCategory = ExportedClassTypeCategory::Class;
		}
		else
		{
			underlyingTypeMapping = GetNativeToScriptTypeMapping(*typeInformation.UnderlyingType);
		}

		return underlyingTypeMapping;
	}
	default:
	case VariableTypeCategory::General:
		return GetNativeToScriptTypeMapping(typeInformation.TypeName);
	}
}

std::string TypeLookup::GetScriptInteropTypeName(const std::string& typeName, bool isResourceReference)
{
	auto iterFind = mNativeToScriptTypeMap.find(typeName);
	if (iterFind == mNativeToScriptTypeMap.end())
		outs() << "Warning: Type \"" << typeName << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.\n";

	bool isValidInteropType = iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Primitive &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Enum &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::String &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::WString &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Path;

	if (!isValidInteropType)
		outs() << "Error: Type \"" << typeName << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.\n";

	std::string cleanName = CleanTemplateParameters(typeName);

	if(isResourceReference)
	{
		if(iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Resource)
			outs() << "Error: Type \"" << typeName << "\" cannot be wrapped in a resource reference.\n";

		return "ScriptRRefBase";
	}
	
	return "Script" + cleanName;
}

std::string TypeLookup::MapCppPrimitiveTypeToCSharpType(const std::string& cppType)
{
	if (cppType == "int8_t")
		return "sbyte";

	if (cppType == "uint8_t")
		return "byte";

	if (cppType == "int16_t")
		return "short";

	if (cppType == "uint16_t")
		return "ushort";

	if (cppType == "int32_t")
		return "int";

	if (cppType == "uint32_t")
		return "int";

	if (cppType == "int64_t")
		return "long";

	if (cppType == "uint64_t")
		return "ulong";

	if (cppType == "wchar_t")
		return "char";

	if (cppType == "char16_t")
		return "ushort";

	if (cppType == "char32_t")
		return "uint";

	return cppType;
}
