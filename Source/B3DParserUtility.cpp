#include "B3DParserUtility.h"
#include "B3DCommentParser.h"
#include "B3DTypeLookup.h"

std::string ParserUtility::GetNamespace(const NamedDecl* decl)
{
	if (decl == nullptr)
		return std::string();

	const DeclContext* context = decl->getDeclContext();

	// Collect contexts.
	SmallVector<const DeclContext *, 8> contexts;
	while (context && isa<NamedDecl>(context))
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	std::string name;
	raw_string_ostream ss(name);
	for (const DeclContext* declContext : reverse(contexts))
	{
		if (const auto *ND = dyn_cast<NamespaceDecl>(declContext))
		{
			if (!ND->isAnonymousNamespace())
				ss << *ND << "::";
		}
	}

	return ss.str();
}

std::string ParserUtility::GetFullName(const NamedDecl* decl)
{
	if (decl == nullptr)
		return std::string();

	const DeclContext* context = decl->getDeclContext();

	// Collect contexts.
	SmallVector<const DeclContext *, 8> contexts;
	while (context && isa<NamedDecl>(context)) 
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	std::string name;
	raw_string_ostream ss(name);
	for (const DeclContext* declContext : reverse(contexts))
	{
		if (const auto *ND = dyn_cast<NamespaceDecl>(declContext))
		{
			if (ND->isAnonymousNamespace())
				ss << "(anonymous namespace)";
			else
				ss << *ND;
		}
		else if (const auto *RD = dyn_cast<RecordDecl>(declContext))
		{
			if (!RD->getIdentifier())
				ss << "(anonymous " << RD->getKindName() << ')';
			else
				ss << *RD;
		}
		else if (const auto *ED = dyn_cast<EnumDecl>(declContext))
		{
			if (ED->isScoped() || ED->getIdentifier())
				ss << *ED;
			else
				continue;
		}
		else
			ss << *cast<NamedDecl>(declContext);

		ss << "::";
	}

	if (decl->getDeclName() || isa<DecompositionDecl>(decl))
		ss << *decl;
	else
		ss << "(anonymous)";

	return ss.str();
}

void ParserUtility::PostProcessFileInfos(CommentParser& commentParser) // TODO - Move to type lookup?
{
	// Inject external methods into their appropriate class infos
	for (auto& entry : externalClassInfos)
	{
		for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
				propertyInfo.Style = methodInfo.Style;

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
						existingInfo.Style = propertyInfo.Style; // Always prefer style flags from the setter

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
			baseClassLookup[baseClassInfo->NativeName].childClasses.push_back(classInfo.NativeName);
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

		int enumIdx = atoi(paramInfo.DefaultValue.c_str());
		const std::string typeName = paramInfo.TypeInformation.GetLastWrappedOrSelfTypeName();
		EnumInfo *const enumInformation = TypeLookup::FindEnumInformation(typeName);
		if(enumInformation == nullptr)
		{
			errs() << "Error: Cannot map default value of \"" + paramInfo.Name + "\" to enum entry for enum type \"" + typeName + "\". Ignoring.";
			paramInfo.DefaultValue = "";
			return;
		}

		auto iterFind = enumInformation->Entries.find(enumIdx);
		if(iterFind == enumInformation->Entries.end())
		{
			errs() << "Error: Cannot map default value of \"" + paramInfo.Name + "\" to enum entry for enum type \"" + typeName + "\". Ignoring.";
			paramInfo.DefaultValue = "";
			return;
		}

		paramInfo.DefaultValue = enumInformation->ScriptName + "." + iterFind->second.ScriptName;
	};

	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
			ClassInfo *const classInfo = TypeLookup::FindClassInformation(typeName, false);
			if (classInfo != nullptr)
			{
				const bool isBase = classInfo->IsFlagSet(ClassFlags::IsBase);
				if (isBase)
				{
					typeInformation.SetPostProcessFlag(VariablePostProcessFlags::IsReferencingBaseClass, true);
				}
			}
		};

		auto markParam = [&fnMarkComplexType,&fnMarkBaseType](VariableInformation& paramInfo)
		{
			fnMarkComplexType(paramInfo.TypeInformation);
			fnMarkBaseType(paramInfo.TypeInformation);
		};

		for (auto& classInfo : fileInfo.second.Classes)
		{
			for(auto& methodInfo : classInfo.Methods)
			{
				for (auto& paramInfo : methodInfo.Parameters)
					markParam(paramInfo);

				if (!methodInfo.ReturnValue.TypeInformation.IsEmpty())
				{
					fnMarkComplexType(methodInfo.ReturnValue.TypeInformation);
					fnMarkBaseType(methodInfo.ReturnValue.TypeInformation);
				}
			}

			for (auto& eventInfo : classInfo.Events)
			{
				for (auto& paramInfo : eventInfo.Parameters)
					markParam(paramInfo);
			}

			for (auto& ctorInfo : classInfo.Constructors)
			{
				for (auto& paramInfo : ctorInfo.Parameters)
					markParam(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.Structs)
		{
			for(auto& fieldInfo : structInfo.Fields)
			{
				fnMarkComplexType(fieldInfo.TypeInformation);
				markParam(fieldInfo);
			}
		}
	}

	// Generate referenced includes
	{
		for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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

				fileInfo.second.ForwardDeclarations.insert(ForwardDeclInfo(classInfo.NativeNameWithoutTemplateArguments, classInfo.Namespace, classInfo.TemplateParameters, classInfo.IsFlagSet(ClassFlags::IsStruct)));

				if (typeInfo.TypeCategory == ::ExportedClassTypeCategory::Resource)
					fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptResource.h");
				else if (typeInfo.TypeCategory == ::ExportedClassTypeCategory::Component)
					fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptComponent.h");
				else if (typeInfo.TypeCategory == ::ExportedClassTypeCategory::SceneObject)
					fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptSceneObject.h");
				else if (typeInfo.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
					fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/GUI/BsScriptGUIElement.h");
				else if (typeInfo.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
					fileInfo.second.ReferencedHeaderIncludes.push_back("Wrappers/BsScriptReflectable.h");
				else // Class
					fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptObject.h");

				if (!classInfo.BaseClassName.empty())
				{
					const TypeMappingInformation& baseTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(classInfo.BaseClassName);

					if(IsAPIEditor(classInfo.API))
						fileInfo.second.ReferencedHeaderIncludes.push_back(baseTypeInfo.EditorInteropFile);
					else
						fileInfo.second.ReferencedHeaderIncludes.push_back(baseTypeInfo.InteropFile);
				}

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

				fileInfo.second.ReferencedHeaderIncludes.push_back("BsScriptObject.h");
				fileInfo.second.ReferencedHeaderIncludes.push_back(typeInfo.NativeFile);
			}

			if(includesInfo.requiresResourceManager)
				fileInfo.second.ReferencedSourceIncludes.push_back("BsScriptResourceManager.h");

			if (includesInfo.requiresRRef)
				fileInfo.second.ReferencedSourceIncludes.push_back("Wrappers/BsScriptRRefBase.h");

			if (includesInfo.requiresAsyncOp)
				fileInfo.second.ReferencedSourceIncludes.push_back("Wrappers/BsScriptAsyncOp.h");

			if(includesInfo.requiresGameObjectManager)
				fileInfo.second.ReferencedSourceIncludes.push_back("BsScriptGameObjectManager.h");

			if(includesInfo.requiresRTTI)
				fileInfo.second.ReferencedSourceIncludes.push_back("Reflection/BsRTTIType.h");

			for (auto& entry : includesInfo.includes)
			{
				uint32_t originFlags = (uint32_t)entry.second.originIncludeFlags;
				uint32_t interopFlags = (uint32_t)entry.second.interopIncludeFlags;

				if (originFlags != 0)
				{
					std::string include = entry.second.typeInfo.NativeFile;

					if ((originFlags & (uint32_t)IncludeType::ForwardDeclare) != 0)
						fileInfo.second.ForwardDeclarations.insert(ForwardDeclInfo(entry.second.typeName, entry.second.typeInfo.NativeNamespace, {}, entry.second.isStruct));

					if((originFlags & (uint32_t)IncludeType::IncludeInImplementation) != 0)
						fileInfo.second.ReferencedSourceIncludes.push_back(include);
					else
						fileInfo.second.ReferencedHeaderIncludes.push_back(include);
				}

				if (interopFlags != 0)
				{
					std::string include;
					if(entry.second.isEditor)
						include = entry.second.typeInfo.EditorInteropFile;
					else
						include = entry.second.typeInfo.InteropFile;

					if ((interopFlags & (uint32_t)IncludeType::ForwardDeclare) != 0)
					{
						if(entry.second.isEditor)
							fileInfo.second.ForwardDeclarations.insert(ForwardDeclInfo(entry.second.typeName, entry.second.typeInfo.NativeNamespace));
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

			for (auto& entry : includesInfo.fwdDecls)
				fileInfo.second.ForwardDeclarations.insert(entry.second);
		}
	}

	// Generate overloads for unsupported default parameters
	for (auto& fileInfo : TypeLookup::GetFilesToGenerateMutable())
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
	}
}

void ParserUtility::PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos)
{
	int firstDefaultParam = -1;
	int lastInvalidParam = -1;
	for (int i = 0; i < methodInfo.Parameters.size(); i++)
	{
		const VariableInformation& param = methodInfo.Parameters[i];

		if (!param.DefaultValue.empty())
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
		MethodInfo copyMethodInfo = methodInfo;

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

		copyMethodInfo.MethodFlags |= (int)MethodFlags::CSOnly;
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

void ParserUtility::GatherIncludes(const VariableTypeInformation& typeInformation, bool isEditor, IncludesInfo& output)
{
	const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(typeInformation);
	const VariableTypeInformation& underlyingTypeInformation = typeInformation.IsArrayOrVector() ? typeInformation.AssertGetUnderlyingType() : typeInformation;

	const std::string& typeName = underlyingTypeInformation.GetLastWrappedOrSelfTypeName();

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource ||
		typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum)
	{
		auto iterFind = output.includes.find(typeName);
		if (iterFind == output.includes.end())
		{
			IncludeType sourceIncludeType = IncludeType::None;
			IncludeType interopIncludeType = typeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum ? IncludeType::IncludeInImplementation : IncludeType::None;
			bool isStruct = false;

			if (underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
			{
				sourceIncludeType = IncludeType::IncludeInImplementation;
				interopIncludeType = IncludeType::None;
			}

			if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
			{
				sourceIncludeType = IncludeType::IncludeInHeader;
				isStruct = true;
			}

			// If enum or passed by value we need to include the header for the source type
			if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum || underlyingTypeInformation.TypeCategory == VariableTypeCategory::General)
				sourceIncludeType = IncludeType::IncludeInHeader;

			output.includes[typeName] = IncludeInfo(typeName, typeMappingInformation, sourceIncludeType, interopIncludeType, isStruct, isEditor);
		}

		if (typeMappingInformation.IsClassType())
		{
			const bool isBase = underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, TypeLookup::GetNativeToScriptTypeMapping(entry), IncludeType::IncludeInImplementation, IncludeType::IncludeInImplementation, false, isEditor);

				output.requiresRTTI = true;
			}
		}
	}

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
		output.fwdDecls[typeName] = ForwardDeclInfo(GetStructInteropTypeName(typeName), typeMappingInformation.NativeNamespace, {}, true);

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
	{
		output.requiresResourceManager = true;

		if (underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
			output.requiresRRef = true;
	}
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject)
		output.requiresGameObjectManager = true;

	if (underlyingTypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
		output.requiresAsyncOp = true;
}

void ParserUtility::GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output)
{
	if (!methodInfo.ReturnValue.TypeInformation.IsEmpty())
		GatherIncludes(methodInfo.ReturnValue.TypeInformation, isEditor, output);

	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
		GatherIncludes(I->TypeInformation, isEditor, output);

	if (methodInfo.IsFlagSet(MethodFlags::External))
	{
		auto iterFind = output.includes.find(methodInfo.ExternalClass);
		if (iterFind == output.includes.end())
		{
			TypeMappingInformation typeInfo = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ExternalClass);
			output.includes[methodInfo.ExternalClass] = IncludeInfo(methodInfo.ExternalClass, typeInfo, IncludeType::ForwardDeclareAndIncludeInImplementation, IncludeType::None, false, isEditor);
		}
	}
}

void ParserUtility::GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output)
{
	const TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInfo.TypeInformation);
	const VariableTypeInformation& underlyingTypeInformation = fieldInfo.TypeInformation.IsArrayOrVector() ? fieldInfo.TypeInformation.AssertGetUnderlyingType() : fieldInfo.TypeInformation;

	const std::string& fieldTypeName = underlyingTypeInformation.GetLastWrappedOrSelfTypeName();

	// These types never require additional includes
	if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Primitive || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::String ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
		return;

	// If passed by value, we needs its header in our header
	if (!underlyingTypeInformation.IsPointerOrHandle())
	{
		const bool isComplexStruct = fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed);
		output.includes[fieldTypeName] = IncludeInfo(fieldTypeName, fieldTypeMappingInformation, IncludeType::IncludeInHeader, isComplexStruct ? IncludeType::IncludeInHeader : IncludeType::None, false, isEditor);
	}

	if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component ||
		fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
	{
		const bool isRRef = underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
		if (!fieldTypeMappingInformation.InteropFile.empty() || isRRef)
		{
			std::string name = "__" + fieldTypeName;
			output.includes[name] = IncludeInfo(fieldTypeName, fieldTypeMappingInformation, IncludeType::IncludeInImplementation, IncludeType::IncludeInImplementation, false, isEditor);
		}

		if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
		{
			output.requiresResourceManager = true;

			if (underlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
				output.requiresRRef = true;
		}
		else if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject)
			output.requiresGameObjectManager = true;
		else if (fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || fieldTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass)
		{
			const bool isBase = underlyingTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(fieldTypeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, TypeLookup::GetNativeToScriptTypeMapping(entry), IncludeType::IncludeInImplementation, IncludeType::IncludeInImplementation, false, isEditor);

				output.requiresRTTI = true;
			}
		}

		if (underlyingTypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
			output.requiresAsyncOp = true;
	}
}

void ParserUtility::GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output)
{
	bool isEditor = IsAPIEditor(classInfo.API);

	for (auto& methodInfo : classInfo.Constructors)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& methodInfo : classInfo.Methods)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& eventInfo : classInfo.Events)
		GatherIncludes(eventInfo, isEditor, output);
}

void ParserUtility::GatherIncludes(const StructInfo& structInfo, IncludesInfo& output)
{
	bool isEditor = IsAPIEditor(structInfo.API);

	if (structInfo.RequiresInteropType)
	{
		for (auto& fieldInfo : structInfo.Fields)
			GatherIncludes(fieldInfo, isEditor, output);
	}
}

bool ParserUtility::CheckIsBuiltinModuleType(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return false;

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			std::string className = baseDecl->getName().str();
			if (className == kBuiltinModuleType)
				return true;

			todo.push(baseDecl);
			iter++;
		}
	}

	return false;
}

bool ParserUtility::IsBuiltinBaseType(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	if (className == kBuiltinComponentType)
		return true;
	else if (className == kBuiltinResourceType)
		return true;
	else if (className == kBuiltinSceneObjectType)
		return true;
	else if (className == kBuiltinModuleType)
		return true;
	else if (className == kBuiltinGUIElementType)
		return true;
	else if (className == kBuiltinReflectableType)
		return true;

	return false;
}

ApiFlags ParserUtility::ParseAPIFromExportFlags(int exportFlags)
{
	int output = 0;

	if((exportFlags & (int)ExportFlags::EngineAPI) != 0)
		output |= (int)ApiFlags::Engine;

	if((exportFlags & (int)ExportFlags::FrameworkAPI) != 0)
		output |= (int)ApiFlags::Framework;

	if((exportFlags & (int)ExportFlags::EditorAPI) != 0)
		output |= (int)ApiFlags::Editor;

	if((int)output == 0)
		output = (int)ApiFlags::Any;

	return (ApiFlags)output;
}

bool ParserUtility::MapBuiltinPrimitiveTypeToCppType(BuiltinType::Kind kind, std::string& output)
{
	switch (kind)
	{
	case BuiltinType::Void:
		output = "void";
		return true;
	case BuiltinType::Bool:
		output = "bool";
		return true;
	case BuiltinType::Char_S:
		output = "char";
		return true;
	case BuiltinType::SChar:
		output = "int8_t";
		return true;
	case BuiltinType::Char_U:
		output = "uint8_t";
		return true;
	case BuiltinType::Short:
		output = "int16_t";
		return true;
	case BuiltinType::Int:
		output = "int32_t";
		return true;
	case BuiltinType::Long:
		output = "int32_t";
		return true;
	case BuiltinType::LongLong:
		output = "int64_t";
		return true;
	case BuiltinType::UChar:
		output = "uint8_t";
		return true;
	case BuiltinType::UShort:
		output = "uint16_t";
		return true;
	case BuiltinType::UInt:
		output = "uint32_t";
		return true;
	case BuiltinType::ULong:
		output = "uint32_t";
		return true;
	case BuiltinType::ULongLong:
		output = "uint64_t";
		return true;
	case BuiltinType::Float:
		output = "float";
		return true;
	case BuiltinType::Double:
		output = "double";
		return true;
	case BuiltinType::WChar_S:
	case BuiltinType::WChar_U:
		output = "wchar_t";
		return true;
	case BuiltinType::Char16:
		output = "char16_t";
		return true;
	case BuiltinType::Char32:
		output = "char32_t";
		return true;
	default:
		break;
	}

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

bool ScriptExportUtility::IsExportAttribute(AnnotateAttr* attr)
{
	StringRef annotation = attr->getAnnotation();

	return annotation.startswith("se,");
}

bool ScriptExportUtility::IsExportable(const CXXRecordDecl* decl)
{
	std::string className = decl->getName().str();

	AnnotateAttr *const attr = decl->getAttr<AnnotateAttr>();
	if (attr != nullptr)
		return IsExportAttribute(attr);

	return false;
}

std::string ScriptExportUtility::FindExportableBaseClassName(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return "";

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			std::string className = baseDecl->getName().str();

			if(ParserUtility::IsBuiltinBaseType(baseDecl))
			{
				iter++;
				continue;
			}

			if (IsExportable(baseDecl))
			{
				StringRef sourceClassName = baseDecl->getName();
				return sourceClassName.str();
			}

			todo.push(baseDecl);
			iter++;
		}
	}

	return "";
}

std::string ScriptExportUtility::FindExportableBasePlainClassName(const CXXRecordDecl* decl)
{
	if (!decl->hasDefinition())
		return "";

	auto iter = decl->bases_begin();
	while (iter != decl->bases_end())
	{
		const CXXBaseSpecifier* baseSpec = iter;
		CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

		std::string className = baseDecl->getName().str();

		AnnotateAttr* attr = baseDecl->getAttr<AnnotateAttr>();
		if (attr != nullptr)
		{
			StringRef sourceClassName = baseDecl->getName();
			ScriptExportInformation parsedDeclInfo;

			if (ParseExportAttribute(attr, sourceClassName, parsedDeclInfo))
			{
				if((parsedDeclInfo.ExportFlags & (int)ExportFlags::ExportAsStruct) != 0)
					return sourceClassName.str();
			}
		}

		iter++;
	}

	return "";
}

void ScriptExportUtility::ParseScriptExportAttributeCommand(const std::string& name, const std::string& value, StringRef sourceName, ScriptExportInformation& output)
{
	if (name == "n" || name == "name")
		output.ExportedTypeName = value;
	else if (name == "v" || name == "visibility")
	{
		if (value == "public")
			output.Visibility = CSVisibility::Public;
		else if (value == "internal")
			output.Visibility = CSVisibility::Internal;
		else if (value == "private")
			output.Visibility = CSVisibility::Private;
		else
			outs() << "Warning: Unrecognized value for \"v\" option: \"" + value + "\" for type \"" <<
			sourceName << "\".\n";
	}
	else if (name == "f" || name == "file")
	{
		output.ExportedFileName = value;
	}
	else if (name == "pl" || name == "plain")
	{
		output.ExportFlags |= (int)ExportFlags::ExportAsStruct;
	}
	else if (name == "pr" || name == "property")
	{
		if (value == "getter")
			output.ExportFlags |= (int)ExportFlags::PropertyGetter;
		else if (value == "setter")
			output.ExportFlags |= (int)ExportFlags::PropertySetter;
		else
		{
			outs() << "Warning: Unrecognized value for \"pr\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "api")
	{
		if (value == "bsf")
			output.ExportFlags |= (int)ExportFlags::FrameworkAPI;
		else if (value == "b3d")
			output.ExportFlags |= (int)ExportFlags::EngineAPI;
		else if (value == "bed")
			output.ExportFlags |= (int)ExportFlags::EditorAPI;
		else
		{
			outs() << "Warning: Unrecognized value for \"pr\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "e")
	{
		output.ExportFlags |= (int)ExportFlags::ExternalMethod;

		output.ExtensionOfType = value;
	}
	else if (name == "ec")
	{
		output.ExportFlags |= (int)ExportFlags::ExternalConstructor;

		output.ExtensionOfType = value;
	}
	else if (name == "ex")
	{
		if (value == "true")
			output.ExportFlags |= (int)ExportFlags::Exclude;
		else if (value != "false")
		{
			outs() << "Warning: Unrecognized value for \"ex\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "in")
	{
		if (value == "true")
			output.ExportFlags |= (int)ExportFlags::InteropOnly;
		else if (value != "false")
		{
			outs() << "Warning: Unrecognized value for \"in\" option: \"" + value + "\" for type \"" <<
				sourceName << "\".\n";
		}
	}
	else if (name == "m")
		output.DocumentationGroup = value;
	else if (name == "hide")
	{
		output.style.flags |= (int)StyleFlags::ForceHide;
	}
	else if (name == "show")
	{
		output.style.flags |= (int)StyleFlags::ForceShow;
	}
	else if (name == "layerMask")
	{
		output.style.flags |= (int)StyleFlags::AsLayerMask;
	}
	else if (name == "slider")
	{
		output.style.flags |= (int)StyleFlags::AsSlider;
	}
	else if (name == "notNull")
	{
		output.style.flags |= (int)StyleFlags::NotNull;
	}
	else if (name == "passByCopy")
	{
		output.style.flags |= (int)StyleFlags::PassByCopy;
	}
	else if (name == "applyOnDirty")
	{
		output.style.flags |= (int)StyleFlags::ApplyOnDirty;
	}
	else if (name == "asQuaternion")
	{
		output.style.flags |= (int)StyleFlags::AsQuaternion;
	}
	else if (name == "loadOnAssign")
	{
		output.style.flags |= (int)StyleFlags::LoadOnAssign;
	}
	else if (name == "hdr")
	{
		output.style.flags |= (int)StyleFlags::HDR;
	}
	else if (name == "step")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"step\" option for type \"" << sourceName << "\".\n";
		else
		{
			output.style.flags |= (int)StyleFlags::Step;
			output.style.step = atof(value.c_str());
		}
	}
	else if (name == "range")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"range\" option for type \"" << sourceName << "\".\n";
		else
		{
			std::vector<float> args;

			std::istringstream toParse(value);
			std::string arg;
			while(std::getline(toParse, arg, ','))
				args.push_back(atof(arg.c_str()));

			if(args.size() != 2)
				outs() << "Warning: Invalid number of arguments for \"range\" option for type \"" << sourceName << "\".\n";
			else
			{
				output.style.flags |= (int)StyleFlags::Range;
				output.style.rangeMin = args[0];
				output.style.rangeMax = args[1];
			}
		}
	}
	else if (name == "order")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"order\" option for type \"" << sourceName << "\".\n";
		else
		{
			output.style.flags |= (int)StyleFlags::Order;
			output.style.order = atoi(value.c_str());
		}
	}
	else if (name == "category")
	{
		if(value.empty())
			outs() << "Warning: Empty value for \"category\" option for type \"" << sourceName << "\".\n";
		else
		{
			std::vector<std::string> args;

			std::istringstream toParse(value);
			std::string arg;
			while(std::getline(toParse, arg, ','))
				args.push_back(arg);

			if(args.size() != 1)
				outs() << "Warning: Invalid number of arguments for \"category\" option for type \"" << sourceName << "\".\n";
			else
			{
				StringRef trimmedName = args[0];
				trimmedName = trimmedName.trim();

				output.style.flags |= (int)StyleFlags::Category;
				output.style.category = trimmedName.str();
			}
		}
	}
	else if (name == "inline")
	{
		output.style.flags |= (int)StyleFlags::Inline;
	}
	else
		outs() << "Warning: Unrecognized annotation attribute option: \"" + name + "\" for type \"" <<
		sourceName << "\".\n";
}

bool ScriptExportUtility::ParseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ScriptExportInformation& output)
{
	if(!IsExportAttribute(attr))
		return false;

	StringRef annotation = attr->getAnnotation();

	output.ExportedTypeName = sourceName.str();
	
	if (!output.ExportedTypeName.empty())
	{
		// Camel case to pascal case
		if(islower(output.ExportedTypeName[0]))
			output.ExportedTypeName[0] = toupper(output.ExportedTypeName[0]);
		else
		{
			// Screaming snake case to pascal case
			bool isScreamingSnakeCase = true;
			std::stringstream caseOutput;
			bool nextUpper = true;
			for(size_t i = 0; i < output.ExportedTypeName.size(); i++)
			{
				if (isalpha(output.ExportedTypeName[i]))
				{
					if(islower(output.ExportedTypeName[i]))
					{
						isScreamingSnakeCase = false;
						break;
					}
					else
					{
						if(!nextUpper)
							caseOutput << (char)tolower(output.ExportedTypeName[i]);
						else
						{
							caseOutput << output.ExportedTypeName[i];
							nextUpper = false;
						}
					}
				}
				else if(output.ExportedTypeName[i] == '_')
					nextUpper = true;
				else
					caseOutput << output.ExportedTypeName[i];
			}

			if(isScreamingSnakeCase)
				output.ExportedTypeName = caseOutput.str();
		}
	}

	output.ExportedFileName = sourceName.str();
	output.Visibility = CSVisibility::Public;
	output.ExportFlags = 0;

	std::stringstream ssParamName;
	std::stringstream ssParamValue;

	bool isInScope = false;
	bool gotParamName = false;

	for (auto iter = annotation.begin() + 3; iter != annotation.end(); ++iter)
	{
		if(*iter == ' ' || *iter == '\t')
			continue;

		if(*iter == '[')
		{
			if(isInScope)
				outs() << "Error: Attribute parameter parsing error. Nested scopes not allowed.";
			else if(!gotParamName)
				outs() << "Error: Attribute parameter parsing error. Scopes not allowed for parameter names.";
			else
				isInScope = true;

			continue;
		}

		if(*iter == ']')
		{
			isInScope = false;
			continue;
		}

		if(*iter == ',')
		{
			if(isInScope)
				ssParamValue << ",";
			else
			{
				ParseScriptExportAttributeCommand(ssParamName.str(), ssParamValue.str(), sourceName, output);
				
				ssParamName.str("");
				ssParamName.clear();

				ssParamValue.str("");
				ssParamValue.clear();

				gotParamName = false;
			}

			continue;
		}

		if(*iter == ':')
		{
			if(gotParamName)
				outs() << "Error: Attribute parameter parsing error. Found value separator while parsing value.";
			else
				gotParamName = true;

			continue;
		}

		if(!gotParamName)
			ssParamName << *iter;
		else
			ssParamValue << *iter;
	}

	if(!ssParamName.str().empty())
		ParseScriptExportAttributeCommand(ssParamName.str(), ssParamValue.str(), sourceName, output);

	return true;
}

bool ScriptExportUtility::ParseExportAttribute(Decl* decl, StringRef sourceName, ScriptExportInformation& output)
{
	for (const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (ParseExportAttribute(entry, sourceName, output))
			return true;
	}

	return false;
}
