#include "B3DTypeLookup.h"
#include "B3DCommentParser.h"

/** Assigns a new method flag to the provided method info. */
static void SetMethodFlag(MethodInfo& methodInfo, MethodFlags flag)
{
	methodInfo.MethodFlags |= (int)flag;
}

/** Does nothing. Ensures templated methods can call this method on different method info types. */
static void SetMethodFlag(StructConstructorInfo& constructorInfo, MethodFlags flag)
{ }

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

bool TypeMappingInformation::IsInt64() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "long" || ScriptTypeName == "ulong");
}

bool TypeMappingInformation::IsInteger() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "int" || ScriptTypeName == "uint" || ScriptTypeName == "long" || ScriptTypeName == "ulong" || ScriptTypeName == "short" || ScriptTypeName == "ushort" || ScriptTypeName == "byte");
}

bool TypeMappingInformation::IsReal() const
{
	return TypeCategory == ExportedClassTypeCategory::Primitive && (ScriptTypeName == "float" || ScriptTypeName == "double");
}

bool TypeMappingInformation::IsHandleType() const
{
	return TypeCategory == ExportedClassTypeCategory::Resource || TypeCategory == ExportedClassTypeCategory::SceneObject || TypeCategory == ExportedClassTypeCategory::Component || TypeCategory == ExportedClassTypeCategory::GameObject;
}

bool TypeMappingInformation::IsClassType() const
{
	return TypeCategory == ExportedClassTypeCategory::Class || TypeCategory == ExportedClassTypeCategory::ReflectableClass || TypeCategory == ExportedClassTypeCategory::IReflectable;
}


std::unordered_map<std::string, FileInfo> TypeLookup::mFilesToGenerate;
std::unordered_map<std::string, TypeMappingInformation> TypeLookup::mNativeToScriptTypeMap;
std::unordered_map<std::string, ExternalClassInfos> TypeLookup::mExternalClassInfos;
std::unordered_map<std::string, TypeLookup::BaseClassInfo> TypeLookup::mBaseClassLookup;

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

ClassInfo* TypeLookup::FindClassInformation(const std::string& typeName, bool preferEditor)
{
	ClassInfo* frameworkClassInfo = nullptr;
	ClassInfo* editorClassInfo = nullptr;
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			if (classInfo.NativeName != typeName)
				continue;

			if(IsAPIFramework(classInfo.API))
			{
				frameworkClassInfo = &classInfo;

				if(!preferEditor)
					return frameworkClassInfo;
			}
			else if(IsAPIEditor(classInfo.API))
			{
				editorClassInfo = &classInfo;

				if(preferEditor)
					return editorClassInfo;
			}
		}
	}

	if(preferEditor)
		return frameworkClassInfo; // Editor version was not found (otherwise we would have returned above), but framework one could be, so return it

	return editorClassInfo; // Framework version was not found (otherwise we would have returned above), but framework one could be, so return it
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

void TypeLookup::RegisterNativeToScriptTypeMapping(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptInteropName, const std::string& scriptFileName, ApiFlags api, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType)
{
	const std::string destinationFile = "BsScript" + scriptFileName + ".generated.h";
	std::string destinationFileEditor = destinationFile;

	// Going to need separate file for editor?
	if (IsAPIEditor(api) && IsAPIFramework(api))
		destinationFileEditor = "BsScript" + scriptFileName + ".editor.generated.h";

	TypeMappingInformation typeMappingInformation = TypeMappingInformation(nameSpace, scriptName, scriptInteropName, typeCategory, nativeFilePath, destinationFile, destinationFileEditor);
	typeMappingInformation.EnumUnderlyingType = enumUnderlyingType;

	mNativeToScriptTypeMap[nativeName] = typeMappingInformation;
}

void TypeLookup::RegisterNativeToScriptTypeMappingWithExplicitPath(const SmallVector<std::string, 4>& nameSpace, const std::string& nativeName, const std::string& nativeFilePath, const std::string& scriptName, const std::string& scriptFilePath, ExportedClassTypeCategory typeCategory, BuiltinType::Kind enumUnderlyingType)
{
	TypeMappingInformation typeMappingInformation = TypeMappingInformation(nameSpace, scriptName, scriptName, typeCategory, nativeFilePath, scriptFilePath);
	typeMappingInformation.EnumUnderlyingType = enumUnderlyingType;

	mNativeToScriptTypeMap[nativeName] = typeMappingInformation;
}

TypeMappingInformation TypeLookup::GetNativeToScriptTypeMapping(const std::string& typeName)
{
	auto iterFind = mNativeToScriptTypeMap.find(typeName);
	if (iterFind == mNativeToScriptTypeMap.end())
	{
		TypeMappingInformation outType;
		outType.TypeCategory = ExportedClassTypeCategory::Primitive;

		if(!MapCppPrimitiveTypeToCSharpType(typeName, outType.ScriptTypeName))
		{
			outType.ScriptTypeName = typeName;
			errs() << "Unable to map type \"" << typeName << "\". Assuming same name as source.\n";
		}

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
		outType.TypeCategory = ExportedClassTypeCategory::Primitive;

		if(!MapCppPrimitiveTypeToCSharpType(typeInformation.TypeName, outType.ScriptTypeName))
		{
			outType.ScriptTypeName = typeInformation.TypeName;
			errs() << "Unable to map type \"" << typeInformation.TypeName << "\". Assuming same name as source.\n";
		}

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
	case VariableTypeCategory::ConstCharString:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "string";
		outType.TypeCategory = ExportedClassTypeCategory::ConstCharString;

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
	case VariableTypeCategory::IReflectable:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "object";
		outType.TypeCategory = ExportedClassTypeCategory::IReflectable;

		return outType;
	}
	case VariableTypeCategory::MonoReflectionType:
	{
		TypeMappingInformation outType;
		outType.ScriptTypeName = "Type";
		outType.TypeCategory = ExportedClassTypeCategory::MonoReflectionType;

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
	case VariableTypeCategory::TInlineArray:
	case VariableTypeCategory::TArray:
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

std::string TypeLookup::GetScriptWrapperObjectTypeName(const std::string& typeName, bool isResourceReference)
{
	auto iterFind = mNativeToScriptTypeMap.find(typeName);
	if (iterFind == mNativeToScriptTypeMap.end())
	{
		outs() << "Warning: Type \"" << typeName << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.\n";
		return "";
	}

	bool isValidInteropType = iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Primitive &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Enum &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::String &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::WString &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::ConstCharString &&
		iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Path;

	if (!isValidInteropType)
		outs() << "Error: Type \"" << typeName << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.\n";

	if(isResourceReference)
	{
		if(iterFind->second.TypeCategory != ::ExportedClassTypeCategory::Resource)
			outs() << "Error: Type \"" << typeName << "\" cannot be wrapped in a resource reference.\n";

		return "ScriptRRefBase";
	}

	return "Script" + iterFind->second.ScriptInteropTypeName;
}

bool TypeLookup::MapCppPrimitiveTypeToCSharpType(const std::string& cppType, std::string& outCsharpType)
{
	if(cppType == "bool")
	{
		outCsharpType = "bool";
		return true;
	}

	if (cppType == "float")
	{
		outCsharpType = "float";
		return true;
	}

	if (cppType == "int32_t")
	{
		outCsharpType = "int";
		return true;
	}

	if (cppType == "uint32_t")
	{
		outCsharpType = "int"; // TODO - Keeping this for legacy reasons, but make this uint eventually
		return true;
	}

	if (cppType == "double")
	{
		outCsharpType = "double";
		return true;
	}

	if (cppType == "char")
	{
		outCsharpType = "char";
		return true;
	}

	if (cppType == "int8_t")
	{
		outCsharpType = "sbyte";
		return true;
	}

	if (cppType == "uint8_t")
	{
		outCsharpType = "byte";
		return true;
	}

	if (cppType == "int16_t")
	{
		outCsharpType = "short";
		return true;
	}

	if (cppType == "uint16_t")
	{
		outCsharpType = "ushort";
		return true;
	}

	if (cppType == "int64_t")
	{
		outCsharpType = "long";
		return true;
	}

	if (cppType == "uint64_t")
	{
		outCsharpType = "ulong";
		return true;
	}

	if (cppType == "wchar_t")
	{
		outCsharpType = "char";
		return true;
	}

	if (cppType == "char16_t")
	{
		outCsharpType = "ushort";
		return true;
	}

	if (cppType == "char32_t")
	{
		outCsharpType = "uint";
		return true;
	}

	return false;
}

void TypeLookup::FinalizeFilesToGenerate(CommentParser& commentParser)
{
	// Inject external methods into their appropriate class infos
	for (auto& entry : mExternalClassInfos)
	{
		for (auto& fileInfo : mFilesToGenerate)
		{
			for (auto& classInfo : fileInfo.second.Classes)
			{
				if (classInfo.NativeName != entry.first)
					continue;

				for (auto& method : entry.second.Methods)
				{
					if (method.IsFlagSet(MethodFlags::Constructor))
					{
						if (method.ReturnValue.TypeInformation.IsEmpty())
						{
							outs() << "Error: Found an external constructor \"" << method.NativeName << "\" with no return value, skipping.\n";
							continue;
						}

						if (method.ReturnValue.TypeInformation.GetLastWrappedOrSelfTypeName() != entry.first)
						{
							outs() << "Error: Found an external constructor \"" << method.NativeName << "\" whose return value doesn't match the external class, skipping.\n";
							continue;
						}
					}
					else
					{
						if (method.Parameters.size() == 0)
						{
							outs() << "Error: Found an external method \"" << method.NativeName << "\" with no parameters. This isn't supported, skipping.\n";
							continue;
						}

						if (method.Parameters[0].TypeInformation.GetLastWrappedOrSelfTypeName() != entry.first)
						{
							outs() << "Error: Found an external method \"" << method.NativeName << "\" whose first parameter doesn't "
								" accept the class its operating on. This is not supported, skipping. \n";
							continue;
						}

						method.Parameters.erase(method.Parameters.begin());
					}

					classInfo.Methods.push_back(method);
				}
			}
		}
	}

	// Resolve copydoc comment commands
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			commentParser.ResolveCopydocComments(classInfo.Documentation, classInfo.NativeName, classInfo.Namespace);

			for (auto& methodInfo : classInfo.Methods)
				commentParser.ResolveCopydocComments(methodInfo.Documentation, classInfo.NativeName, classInfo.Namespace);

			for (auto& ctorInfo : classInfo.Constructors)
				commentParser.ResolveCopydocComments(ctorInfo.Documentation, classInfo.NativeName, classInfo.Namespace);

			for (auto& eventInfo : classInfo.Events)
				commentParser.ResolveCopydocComments(eventInfo.Documentation, classInfo.NativeName, classInfo.Namespace);
		}

		for (auto& structInfo : fileInfo.second.Structs)
			commentParser.ResolveCopydocComments(structInfo.Documentation, structInfo.NativeName, structInfo.Namespace);

		for(auto& enumInfo : fileInfo.second.Enums)
		{
			commentParser.ResolveCopydocComments(enumInfo.Documentation, enumInfo.NativeName, enumInfo.Namespace);

			for (auto& enumEntryInfo : enumInfo.Entries)
				commentParser.ResolveCopydocComments(enumEntryInfo.second.Documentation, enumInfo.NativeName, enumInfo.Namespace);
		}
	}

	// Generate unique interop method names
	std::unordered_set<std::string> usedNames;
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			usedNames.clear();

			auto generateInteropName = [&usedNames](MethodInfo& methodInfo)
			{
				std::string interopName = methodInfo.NativeName;
				int counter = 0;
				while (true)
				{
					auto iterFind = usedNames.find(interopName);
					if (iterFind == usedNames.end())
						break;

					interopName = methodInfo.NativeName + std::to_string(counter);
					counter++;
				}

				usedNames.insert(interopName);
				methodInfo.InteropName = interopName;
			};

			for (auto& methodInfo : classInfo.Methods)
				generateInteropName(methodInfo);

			for (auto& methodInfo : classInfo.Constructors)
				generateInteropName(methodInfo);

			for (auto& eventInfo : classInfo.Events)
				generateInteropName(eventInfo);
		}
	}

	// Generate property infos
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			for (auto& methodInfo : classInfo.Methods)
			{
				bool isGetter = methodInfo.IsFlagSet(MethodFlags::PropertyGetter);
				bool isSetter = methodInfo.IsFlagSet(MethodFlags::PropertySetter);

				if (!isGetter && !isSetter)
					continue;

				PropertyInfo propertyInfo;
				propertyInfo.ScriptName = methodInfo.ScriptName;
				propertyInfo.Documentation = methodInfo.Documentation;
				propertyInfo.IsStatic = methodInfo.IsFlagSet(MethodFlags::Static);
				propertyInfo.Visibility = methodInfo.Visibility;
				propertyInfo.API = methodInfo.API;
				propertyInfo.MetaData = methodInfo.MetaData;

				if (isGetter)
				{
					propertyInfo.GetterName = methodInfo.InteropName;
					propertyInfo.TypeInformation = methodInfo.ReturnValue.TypeInformation;
				}
				else // Setter
				{
					propertyInfo.SetterName = methodInfo.InteropName;
					propertyInfo.TypeInformation = methodInfo.Parameters[0].TypeInformation;
				}

				auto iterFind = std::find_if(classInfo.Properties.begin(), classInfo.Properties.end(),
					[&propertyInfo](const PropertyInfo& info)
				{
					return propertyInfo.ScriptName == info.ScriptName;
				});

				if (iterFind == classInfo.Properties.end())
					classInfo.Properties.push_back(propertyInfo);
				else
				{
					PropertyInfo& existingInfo = *iterFind;
					if (existingInfo.TypeInformation.GetLastWrappedOrSelfTypeName() != propertyInfo.TypeInformation.GetLastWrappedOrSelfTypeName() || existingInfo.IsStatic != propertyInfo.IsStatic)
					{
						outs() << "Error: Getter and setter types for the property \"" << propertyInfo.ScriptName << "\" don't match. Skipping property." << existingInfo.TypeInformation.GetLastWrappedOrSelfTypeName() << " " << propertyInfo.TypeInformation.GetLastWrappedOrSelfTypeName() << "\n";
						continue;
					}

					if (!propertyInfo.GetterName.empty())
					{
						existingInfo.GetterName = propertyInfo.GetterName;

						// Prefer documentation from setter, but use getter if no other available
						if (existingInfo.Documentation.Brief.empty())
							existingInfo.Documentation = propertyInfo.Documentation;
					}
					else
					{
						existingInfo.SetterName = propertyInfo.SetterName;
						existingInfo.MetaData = propertyInfo.MetaData; // Always prefer style flags from the setter

						if (!propertyInfo.Documentation.Brief.empty())
							existingInfo.Documentation = propertyInfo.Documentation;
					}
				}
			}
		}
	}

	// Generate meta-data about base classes
	for (const auto& fileInfo : TypeLookup::GetFilesToGenerate())
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			if (classInfo.BaseClassName.empty())
				continue;

			bool isEditor = IsAPIEditor(classInfo.API);
			ClassInfo* baseClassInfo = TypeLookup::FindClassInformation(classInfo.BaseClassName, isEditor);
			if (baseClassInfo == nullptr)
			{
				assert(false);
				continue;
			}

			baseClassInfo->ClassFlags |= (int)ClassFlags::IsBase;
			mBaseClassLookup[baseClassInfo->NativeName].ChildClasses.push_back(classInfo.NativeName);
		}
	}

	// Properly generate enum default values
	auto parseDefaultValue = [&](VariableInformation& paramInfo)
	{
		if (paramInfo.DefaultValue.empty())
			return;

		TypeMappingInformation typeInfo = TypeLookup::GetNativeToScriptTypeMapping(paramInfo.TypeInformation);

		if (typeInfo.TypeCategory != ::ExportedClassTypeCategory::Enum)
			return;

		const std::string typeName = paramInfo.TypeInformation.GetLastWrappedOrSelfTypeName();
		EnumInfo *const enumInformation = TypeLookup::FindEnumInformation(typeName);
		if(enumInformation == nullptr)
		{
			errs() << "Error: Cannot map default value of \"" + paramInfo.Name + "\" to enum entry for enum type \"" + typeName + "\". Ignoring.";
			paramInfo.DefaultValue = "";
			return;
		}

		bool foundEnumEntry = false;
		for(auto I = enumInformation->Entries.begin(); I != enumInformation->Entries.end(); ++I)
		{
			if(I->second.Value == paramInfo.DefaultValue)
			{
				paramInfo.DefaultValue = enumInformation->ScriptName + "." + I->second.ScriptName;
				foundEnumEntry = true;
				break;
			}
		}

		if(!foundEnumEntry)
		{
			errs() << "Error: Cannot map default value of \"" + paramInfo.Name + "\" to enum entry for enum type \"" + typeName + "\". Ignoring.";
			paramInfo.DefaultValue = "";
			return;
		}
	};

	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			for(auto& methodInfo : classInfo.Methods)
			{
				for (auto& paramInfo : methodInfo.Parameters)
					parseDefaultValue(paramInfo);
			}

			for (auto& ctorInfo : classInfo.Constructors)
			{
				for (auto& paramInfo : ctorInfo.Parameters)
					parseDefaultValue(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.Structs)
		{
			for(auto& fieldInfo : structInfo.Fields)
				parseDefaultValue(fieldInfo);

			for (auto& ctorInfo : structInfo.Constructors)
			{
				for (auto& paramInfo : ctorInfo.Parameters)
					parseDefaultValue(paramInfo);
			}
		}
	}

	// Find structs requiring special conversion
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& structInfo : fileInfo.second.Structs)
		{
			for(auto& fieldInformation : structInfo.Fields)
			{
				const TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

				if(fieldInformation.TypeInformation.IsArrayOrVector() || !(fieldTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Primitive || fieldTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Enum))
				{
					structInfo.RequiresInteropType = true;
					break;
				}
			}

			if (structInfo.RequiresInteropType)
				structInfo.InteropName = GetStructInteropTypeName(structInfo.NativeName);
			else
				structInfo.InteropName = structInfo.NativeName;
		}
	}

	// Mark parameters referencing complex structs and base types
	for (auto& fileInfo : mFilesToGenerate)
	{
		auto fnMarkComplexType = [](VariableTypeInformation& typeInformation)
		{
			const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(typeInformation);
			if (typeMappingInformation.TypeCategory != ExportedClassTypeCategory::Struct)
				return;

			const std::string& typeName = typeInformation.GetLastWrappedOrSelfTypeName();
			StructInfo *const structInfo = TypeLookup::FindStructInformation(typeName);
			if (structInfo != nullptr && structInfo->RequiresInteropType)
			{
				typeInformation.SetPostProcessFlag(VariablePostProcessFlags::IsStructWrapperUsed, true);
			}
		};

		auto fnMarkBaseType = [](VariableTypeInformation& typeInformation)
		{
			const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(typeInformation);
			if (typeMappingInformation.TypeCategory != ExportedClassTypeCategory::Class && typeMappingInformation.TypeCategory != ExportedClassTypeCategory::ReflectableClass &&
				typeMappingInformation.TypeCategory != ExportedClassTypeCategory::GUIElement && !typeMappingInformation.IsHandleType())
				return;

			const std::string& typeName = typeInformation.GetLastWrappedOrSelfTypeName();
			ClassInfo *const classInfo = TypeLookup::FindClassInformation(typeName, true);
			if (classInfo != nullptr)
			{
				const bool isBase = classInfo->IsFlagSet(ClassFlags::IsBase);
				if (isBase)
				{
					typeInformation.SetPostProcessFlag(VariablePostProcessFlags::IsReferencingBaseClass, true);
				}
			}
		};

		auto fnMarkParameter = [&fnMarkComplexType, &fnMarkBaseType](VariableInformation& paramInfo)
		{
			fnMarkComplexType(paramInfo.TypeInformation);
			fnMarkBaseType(paramInfo.TypeInformation);
		};

		for (auto& classInfo : fileInfo.second.Classes)
		{
			for(auto& methodInfo : classInfo.Methods)
			{
				for (auto& paramInfo : methodInfo.Parameters)
					fnMarkParameter(paramInfo);

				if (!methodInfo.ReturnValue.TypeInformation.IsEmpty())
				{
					fnMarkComplexType(methodInfo.ReturnValue.TypeInformation);
					fnMarkBaseType(methodInfo.ReturnValue.TypeInformation);
				}
			}

			for (auto& eventInfo : classInfo.Events)
			{
				for (auto& paramInfo : eventInfo.Parameters)
					fnMarkParameter(paramInfo);
			}

			for (auto& ctorInfo : classInfo.Constructors)
			{
				for (auto& paramInfo : ctorInfo.Parameters)
					fnMarkParameter(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.Structs)
		{
			for(auto& fieldInfo : structInfo.Fields)
				fnMarkParameter(fieldInfo);
		}
	}

	// Generate correct script type names for templates, also only ensure a single C# class gets generated
	{
		std::unordered_set<std::string> processedTemplatedTypes;

		for (auto& fileInfo : mFilesToGenerate)
		{
			// TODO - This should be done in order, so those with least nested template parameters are parsed first
			auto fnGenerateTemplateScriptName = [](GeneratedTypeInformation& typeInfo)
			{
				std::stringstream stream;
				stream << typeInfo.NativeNameWithoutTemplateArguments << "<";

				for(size_t templateParameterIndex = 0; templateParameterIndex < typeInfo.TemplateParameters.size(); ++templateParameterIndex)
				{
					if(templateParameterIndex != 0)
						stream << ",";

					stream << GetNativeToScriptTypeMapping(typeInfo.TemplateParameters[templateParameterIndex].Value).ScriptTypeName;
				}

				stream << ">";

				auto found = mNativeToScriptTypeMap.find(typeInfo.NativeName);
				if(found != mNativeToScriptTypeMap.end())
					found->second.ScriptTypeName = stream.str();
			};

			for (auto& classInfo : fileInfo.second.Classes)
			{
				if(!classInfo.IsFlagSet(ClassFlags::IsTemplate))
					continue;

				auto insertResult = processedTemplatedTypes.insert(classInfo.NativeNameWithoutTemplateArguments);
				if(!insertResult.second)
					classInfo.ClassFlags |= (int)ClassFlags::SkipGeneratingCSharp;

				fnGenerateTemplateScriptName(classInfo);
			}

			for(auto& structInfo : fileInfo.second.Structs)
			{
				if(!structInfo.IsFlagSet(StructFlags::IsTemplate))
					continue;

				auto insertResult = processedTemplatedTypes.insert(structInfo.NativeNameWithoutTemplateArguments);
				if(!insertResult.second)
					structInfo.StructFlags |= (int)StructFlags::SkipGeneratingCSharp;

				fnGenerateTemplateScriptName(structInfo);
			}
		}
	}

	// Generate referenced includes
	{
		for (auto& fileInfo : mFilesToGenerate)
		{
			IncludesInfo includesInfo;
			for (auto& classInfo : fileInfo.second.Classes)
				GatherIncludes(classInfo, includesInfo);

			for (auto& structInfo : fileInfo.second.Structs)
				GatherIncludes(structInfo, includesInfo);

			// Needed for all .h files
			if (!fileInfo.second.InEditor)
				fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptEnginePrerequisites.h");
			else
				fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptEditorPrerequisites.h");

			// Needed for all .cpp files
			fileInfo.second.ReferencedSourceIncludes.push_back("BsScript" + fileInfo.first + ".generated.h");
			fileInfo.second.ReferencedSourceIncludes.push_back("BsMonoMethod.h");
			fileInfo.second.ReferencedSourceIncludes.push_back("BsMonoClass.h");
			fileInfo.second.ReferencedSourceIncludes.push_back("BsMonoUtil.h");

			for (auto& classInfo : fileInfo.second.Classes)
			{
				const TypeMappingInformation& typeInfo = TypeLookup::GetNativeToScriptTypeMapping(classInfo.NativeName);

				// Forward declare the native type we're generating the wrapper for, or if non-reflectable include the type header
				if(typeInfo.TypeCategory != ExportedClassTypeCategory::Class)
				{
					fileInfo.second.ForwardDeclarations.insert(ForwardDeclarationInformation(classInfo.NativeNameWithoutTemplateArguments, classInfo.Namespace, classInfo.TemplateParameters, classInfo.IsFlagSet(ClassFlags::IsStruct)));
				}
				else
					fileInfo.second.ReferencedHeaderIncludes.push_back(typeInfo.NativeFile);

				// Include the script wrapper object root base type
				if(classInfo.HasGlobalSingleInstance())
					fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptTypeDefinition.h");
				else
				{
					if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::Resource)
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptResourceWrapper.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::GameObject)
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptGameObjectWrapper.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::Component)
						fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptComponent.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::SceneObject)
						fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptSceneObject.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptGUIElementWrapper.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass || typeInfo.TypeCategory == ::ExportedClassTypeCategory::IReflectable)
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptReflectableWrapper.h");
					else if(typeInfo.TypeCategory == ::ExportedClassTypeCategory::Class)
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptNonReflectableWrapper.h");
					else // Struct or enum
						fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptTypeDefinition.h");
				}

				// If class has a base type, include its script object wrapper file
				if (!classInfo.BaseClassName.empty())
				{
					const TypeMappingInformation& baseTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(classInfo.BaseClassName);

					if(IsAPIEditor(classInfo.API))
						fileInfo.second.ReferencedHeaderIncludes.push_back(baseTypeInfo.EditorInteropFile);
					else
						fileInfo.second.ReferencedHeaderIncludes.push_back(baseTypeInfo.InteropFile);
				}

				// Include native type
				if (typeInfo.TypeCategory != ::ExportedClassTypeCategory::ReflectableClass && classInfo.TemplateParameters.empty())
					fileInfo.second.ReferencedSourceIncludes.push_back(typeInfo.NativeFile);
				else
				{
					// Templated classes need to be included in header, so the linker doesn't instantiate them multiple times for different libraries
					// (in case template is exported).
					// Reflectable classes need to be included in the header because they provide a getInternal<T>() method
					// which requires information about T.
					fileInfo.second.ReferencedHeaderIncludes.push_back(typeInfo.NativeFile);
				}
			}

			for(auto& structInfo : fileInfo.second.Structs)
			{
				const TypeMappingInformation& typeInfo = TypeLookup::GetNativeToScriptTypeMapping(structInfo.NativeName);

				fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptObjectWrapper.h");
				fileInfo.second.ReferencedHeaderIncludes.push_back(typeInfo.NativeFile);
			}

			if(includesInfo.RequiresScriptResourceManager)
				fileInfo.second.ReferencedSourceIncludes.push_back("BsScriptResourceManager.h");

			if (includesInfo.RequiresScriptRRef)
				fileInfo.second.ReferencedSourceIncludes.push_back("Wrappers/BsScriptRRefBase.h");

			if (includesInfo.RequiresAsyncOp)
				fileInfo.second.ReferencedSourceIncludes.push_back("Wrappers/BsScriptAsyncOp.h");

			if(includesInfo.RequiresRTTI)
				fileInfo.second.ReferencedSourceIncludes.push_back("Reflection/BsRTTIType.h");

			if(includesInfo.RequiresScriptAssemblyManager)
				fileInfo.second.ReferencedSourceIncludes.push_back("Serialization/BsScriptAssemblyManager.h");

			for (auto& entry : includesInfo.Includes)
			{
				uint32_t originFlags = (uint32_t)entry.second.NativeIncludeFlags;
				uint32_t interopFlags = (uint32_t)entry.second.InteropIncludeFlags;

				if (originFlags != 0)
				{
					std::string include = entry.second.TypeMappingInfo.NativeFile;

					if ((originFlags & (uint32_t)IncludeType::ForwardDeclare) != 0)
						fileInfo.second.ForwardDeclarations.insert(ForwardDeclarationInformation(entry.second.NativeTypeName, entry.second.TypeMappingInfo.NativeNamespace, {}, entry.second.IsStruct));

					if((originFlags & (uint32_t)IncludeType::IncludeInImplementation) != 0)
						fileInfo.second.ReferencedSourceIncludes.push_back(include);
					else
						fileInfo.second.ReferencedHeaderIncludes.push_back(include);
				}

				if (interopFlags != 0)
				{
					std::string include;
					if(entry.second.IsEditor)
						include = entry.second.TypeMappingInfo.EditorInteropFile;
					else
						include = entry.second.TypeMappingInfo.InteropFile;

					if ((interopFlags & (uint32_t)IncludeType::ForwardDeclare) != 0)
					{
						if(entry.second.IsEditor)
							fileInfo.second.ForwardDeclarations.insert(ForwardDeclarationInformation(entry.second.NativeTypeName, entry.second.TypeMappingInfo.NativeNamespace));
					}

					if(!include.empty())
					{
						if ((interopFlags & (uint32_t)IncludeType::IncludeInImplementation) != 0)
							fileInfo.second.ReferencedSourceIncludes.push_back(include);
						else
							fileInfo.second.ReferencedHeaderIncludes.push_back(include);
					}
				}
			}

			for (auto& entry : includesInfo.ForwardDeclarations)
				fileInfo.second.ForwardDeclarations.insert(entry.second);
		}
	}

	// Generate overloads for unsupported default parameters
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			std::vector<MethodInfo> newMethodInfos;
			for (auto& methodInfo : classInfo.Methods)
				PostProcessDefaultParameters(methodInfo, newMethodInfos);

			for (auto& methodInfo : newMethodInfos)
				classInfo.Methods.push_back(methodInfo);

			std::vector<MethodInfo> newCtorInfos;
			for (auto& ctorInfo : classInfo.Constructors)
				PostProcessDefaultParameters(ctorInfo, newCtorInfos);

			for (auto& ctorInfo : newCtorInfos)
				classInfo.Constructors.push_back(ctorInfo);
		}

		for(auto& structInfo : fileInfo.second.Structs)
		{
			std::vector<StructConstructorInfo> newConstructorInfos;
			for(auto& constructorInfo : structInfo.Constructors)
				PostProcessDefaultParameters(constructorInfo, newConstructorInfos);

			for (auto& constructorInfo : newConstructorInfos)
				structInfo.Constructors.push_back(constructorInfo);
		}
	}
}

template<class T>
void TypeLookup::PostProcessDefaultParameters(T& methodInfo, std::vector<T>& newMethodInfos)
{
	int firstDefaultParam = -1;
	int lastInvalidParam = -1;
	for (int i = 0; i < methodInfo.Parameters.size(); i++)
	{
		const VariableInformation& param = methodInfo.Parameters[i];

		if (!param.DefaultValue.empty() || !param.DefaultValueType.empty())
		{
			firstDefaultParam = i;
			break;
		}
	}

	for (int i = 0; i < methodInfo.Parameters.size(); i++)
	{
		const VariableInformation& parameterInformation = methodInfo.Parameters[i];

		if (!parameterInformation.DefaultValueType.empty() && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::Flags)
			lastInvalidParam = i;
	}

	// Nothing to handle
	if (lastInvalidParam == -1)
		return;

	// Mark any non-complex default params as complex, so the generator doesn't generate them (since default arguments
	// must follow them, which they can't because at least one is complex)
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VariableInformation& param = methodInfo.Parameters[i];

		if (param.DefaultValueType.empty())
			param.DefaultValueType = "null";
	}

	// Generate a method for each default param
	for (int i = lastInvalidParam; i >= firstDefaultParam; i--)
	{
		T copyMethodInfo = methodInfo;

		// Clear all param default values
		for (int j = firstDefaultParam; j < i; j++)
		{
			VariableInformation& param = copyMethodInfo.Parameters[j];
			param.DefaultValue = "";
			param.DefaultValueType = "";
		}

		// Erase docs for the params we'll skip during generation
		CommentEntry& docs = copyMethodInfo.Documentation;
		for (int j = i; j <= lastInvalidParam; j++)
		{
			const std::string& paramName = copyMethodInfo.Parameters[j].Name;

			for (auto iter = docs.ParameterComments.begin(); iter != docs.ParameterComments.end();)
			{
				if (iter->Name == paramName)
					iter = docs.ParameterComments.erase(iter);
				else
					++iter;
			}
		}

		SetMethodFlag(copyMethodInfo, MethodFlags::CSOnly);
		newMethodInfos.push_back(copyMethodInfo);
	}

	// Clear default params from this method
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VariableInformation& param = methodInfo.Parameters[i];
		param.DefaultValue = "";
		param.DefaultValueType = "";
	}
}

void TypeLookup::GatherIncludes(const VariableTypeInformation& typeInformation, bool isEditor, IncludesInfo& output)
{
	const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(typeInformation);
	const VariableTypeInformation& underlyingTypeInformation = typeInformation.IsArrayOrVector() ? typeInformation.AssertGetUnderlyingType() : typeInformation;

	const std::string& typeName = underlyingTypeInformation.GetLastWrappedOrSelfTypeName();

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GameObject ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement)
	{
		auto iterFind = output.Includes.find(typeName);
		if (iterFind == output.Includes.end())
		{
			IncludeType sourceIncludeType = IncludeType::None;
			IncludeType interopIncludeType = typeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum ? IncludeType::IncludeInImplementation : IncludeType::None;
			bool isStruct = false;

			if (underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
			{
				sourceIncludeType = IncludeType::IncludeInImplementation;
				interopIncludeType = IncludeType::None;
			}

			// Game object handles need the full type definition
			if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component)
				sourceIncludeType = IncludeType::IncludeInImplementation;

			if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
			{
				sourceIncludeType = IncludeType::IncludeInHeader;
				isStruct = true;
			}

			// If enum or passed by value we need to include the header for the source type
			if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum || underlyingTypeInformation.TypeCategory == VariableTypeCategory::General)
				sourceIncludeType = IncludeType::IncludeInHeader;

			output.Includes[typeName] = IncludeInfo(typeName, typeMappingInformation, sourceIncludeType, interopIncludeType, isStruct, isEditor);
		}

		if (typeMappingInformation.IsClassType())
		{
			const bool isBase = underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);
			if (isBase)
				output.RequiresRTTI = true;
		}
	}

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
		output.ForwardDeclarations[typeName] = ForwardDeclarationInformation(GetStructInteropTypeName(typeName), typeMappingInformation.NativeNamespace, {}, true);

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
	{
		if(underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
		{
			output.RequiresScriptRRef = true;
			output.RequiresScriptResourceManager = true;
		}
	}

	if (underlyingTypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
		output.RequiresAsyncOp = true;

	if(underlyingTypeInformation.TypeCategory == VariableTypeCategory::MonoReflectionType && typeInformation.IsArrayOrVector())
		output.RequiresScriptAssemblyManager = true;
}

void TypeLookup::GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output)
{
	if (!methodInfo.ReturnValue.TypeInformation.IsEmpty())
		GatherIncludes(methodInfo.ReturnValue.TypeInformation, isEditor, output);

	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
		GatherIncludes(I->TypeInformation, isEditor, output);

	if (methodInfo.IsFlagSet(MethodFlags::External))
	{
		auto iterFind = output.Includes.find(methodInfo.ExternalClass);
		if (iterFind == output.Includes.end())
		{
			TypeMappingInformation typeInfo = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ExternalClass);
			output.Includes[methodInfo.ExternalClass] = IncludeInfo(methodInfo.ExternalClass, typeInfo, IncludeType::ForwardDeclareAndIncludeInImplementation, IncludeType::None, false, isEditor);
		}
	}
}

void TypeLookup::GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output)
{
	const TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInfo.TypeInformation);
	const VariableTypeInformation& underlyingTypeInformation = fieldInfo.TypeInformation.IsArrayOrVector() ? fieldInfo.TypeInformation.AssertGetUnderlyingType() : fieldInfo.TypeInformation;

	const std::string& fieldTypeName = underlyingTypeInformation.GetLastWrappedOrSelfTypeName();

	// These types never require additional includes
	if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Primitive || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::String ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ConstCharString ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
		return;

	// If passed by value, we needs its header in our header
	if (!underlyingTypeInformation.IsPointerOrHandle())
	{
		const bool isComplexStruct = fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed);
		output.Includes[fieldTypeName] = IncludeInfo(fieldTypeName, fieldTypeMappingInformation, IncludeType::IncludeInHeader, isComplexStruct ? IncludeType::IncludeInHeader : IncludeType::None, false, isEditor);
	}

	if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::GameObject ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
	{
		const bool isRRef = underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
		if (!fieldTypeMappingInformation.InteropFile.empty() || isRRef)
		{
			std::string name = "__" + fieldTypeName;
			output.Includes[name] = IncludeInfo(fieldTypeName, fieldTypeMappingInformation, IncludeType::IncludeInImplementation, IncludeType::IncludeInImplementation, false, isEditor);
		}

		if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
		{
			if(underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
			{
				output.RequiresScriptResourceManager = true;
				output.RequiresScriptRRef = true;
			}
		}
		else if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass)
		{
			const bool isBase = underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);
			if (isBase)
				output.RequiresRTTI = true;
		}

		if (underlyingTypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
			output.RequiresAsyncOp = true;
	}

	if(underlyingTypeInformation.TypeCategory == VariableTypeCategory::MonoReflectionType && fieldInfo.TypeInformation.IsArrayOrVector())
		output.RequiresScriptAssemblyManager = true;
}

void TypeLookup::GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output)
{
	bool isEditor = IsAPIEditor(classInfo.API);

	for (auto& methodInfo : classInfo.Constructors)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& methodInfo : classInfo.Methods)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& eventInfo : classInfo.Events)
		GatherIncludes(eventInfo, isEditor, output);
}

void TypeLookup::GatherIncludes(const StructInfo& structInfo, IncludesInfo& output)
{
	bool isEditor = IsAPIEditor(structInfo.API);

	if (structInfo.RequiresInteropType)
	{
		for (auto& fieldInfo : structInfo.Fields)
			GatherIncludes(fieldInfo, isEditor, output);
	}
}

void TypeLookup::GetDerivedClasses(const std::string& typeName, std::vector<std::string>& output, bool onlyDirect)
{
	auto iterFind = mBaseClassLookup.find(typeName);
	if(iterFind == mBaseClassLookup.end())
		return;

	for(auto& entry : iterFind->second.ChildClasses)
	{
		output.push_back(entry);

		if(!onlyDirect)
			GetDerivedClasses(entry, output);
	}
}

void TypeLookup::RegisterExternalMethod(const std::string& typeName, const MethodInfo& methodInfo)
{
	ExternalClassInfos& infos = mExternalClassInfos[typeName];
	infos.Methods.push_back(methodInfo);
}

