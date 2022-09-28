#include "B3DParserUtility.h"
#include "B3DCommentParser.h"

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

void ParserUtility::PostProcessFileInfos(CommentParser& commentParser)
{
	// Inject external methods into their appropriate class infos
	auto findClassInfo = [](const std::string& name, bool isEditor) -> ClassInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != name)
					continue;

				// Two versions of editor and Framework class migth exist, make sure to pick the right one
				if((isEditor && classInfo.api == ApiFlags::Framework) || (!isEditor &&  hasAPIBED(classInfo.api)))
					continue;

				return &classInfo;
			}
		}

		return nullptr;
	};

	auto findEnumInfo = [](const std::string& name) -> EnumInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& enumInfo : fileInfo.second.enumInfos)
			{
				if (enumInfo.name == name)
					return &enumInfo;
			}
		}

		return nullptr;
	};

	for (auto& entry : externalClassInfos)
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != entry.first)
					continue;

				for (auto& method : entry.second.methods)
				{
					if (((int)method.flags & (int)MethodFlags::Constructor) != 0)
					{
						if (method.returnInfo.typeName.size() == 0)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" with no return value, skipping.\n";
							continue;
						}

						if (method.returnInfo.typeName != entry.first)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" whose return value doesn't match the external class, skipping.\n";
							continue;
						}
					}
					else
					{
						if (method.paramInfos.size() == 0)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" with no parameters. This isn't supported, skipping.\n";
							continue;
						}

						if (method.paramInfos[0].typeName != entry.first)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" whose first parameter doesn't "
								" accept the class its operating on. This is not supported, skipping. \n";
							continue;
						}

						method.paramInfos.erase(method.paramInfos.begin());
					}

					classInfo.methodInfos.push_back(method);
				}
			}
		}
	}

	// Resolve copydoc comment commands
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			commentParser.ResolveCopydocComments(classInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& methodInfo : classInfo.methodInfos)
				commentParser.ResolveCopydocComments(methodInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& ctorInfo : classInfo.ctorInfos)
				commentParser.ResolveCopydocComments(ctorInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& eventInfo : classInfo.eventInfos)
				commentParser.ResolveCopydocComments(eventInfo.documentation, classInfo.name, classInfo.ns);
		}

		for (auto& structInfo : fileInfo.second.structInfos)
			commentParser.ResolveCopydocComments(structInfo.documentation, structInfo.name, structInfo.ns);

		for(auto& enumInfo : fileInfo.second.enumInfos)
		{
			commentParser.ResolveCopydocComments(enumInfo.documentation, enumInfo.name, enumInfo.ns);

			for (auto& enumEntryInfo : enumInfo.entries)
				commentParser.ResolveCopydocComments(enumEntryInfo.second.Documentation, enumInfo.name, enumInfo.ns);
		}
	}

	// Generate unique interop method names
	std::unordered_set<std::string> usedNames;
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			usedNames.clear();

			auto generateInteropName = [&usedNames](MethodInfo& methodInfo)
			{
				std::string interopName = methodInfo.sourceName;
				int counter = 0;
				while (true)
				{
					auto iterFind = usedNames.find(interopName);
					if (iterFind == usedNames.end())
						break;

					interopName = methodInfo.sourceName + std::to_string(counter);
					counter++;
				}

				usedNames.insert(interopName);
				methodInfo.interopName = interopName;
			};

			for (auto& methodInfo : classInfo.methodInfos)
				generateInteropName(methodInfo);

			for (auto& methodInfo : classInfo.ctorInfos)
				generateInteropName(methodInfo);

			for (auto& eventInfo : classInfo.eventInfos)
				generateInteropName(eventInfo);
		}
	}

	// Generate property infos
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for (auto& methodInfo : classInfo.methodInfos)
			{
				bool isGetter = (methodInfo.flags & (int)MethodFlags::PropertyGetter) != 0;
				bool isSetter = (methodInfo.flags & (int)MethodFlags::PropertySetter) != 0;

				if (!isGetter && !isSetter)
					continue;

				PropertyInfo propertyInfo;
				propertyInfo.name = methodInfo.scriptName;
				propertyInfo.documentation = methodInfo.documentation;
				propertyInfo.isStatic = (methodInfo.flags & (int)MethodFlags::Static);
				propertyInfo.visibility = methodInfo.visibility;
				propertyInfo.api = methodInfo.api;
				propertyInfo.style = methodInfo.style;

				if (isGetter)
				{
					propertyInfo.getter = methodInfo.interopName;
					propertyInfo.type = methodInfo.returnInfo.typeName;
					propertyInfo.typeFlags = methodInfo.returnInfo.flags;
				}
				else // Setter
				{
					propertyInfo.setter = methodInfo.interopName;
					propertyInfo.type = methodInfo.paramInfos[0].typeName;
					propertyInfo.typeFlags = methodInfo.paramInfos[0].flags;
				}

				auto iterFind = std::find_if(classInfo.propertyInfos.begin(), classInfo.propertyInfos.end(),
					[&propertyInfo](const PropertyInfo& info)
				{
					return propertyInfo.name == info.name;
				});

				if (iterFind == classInfo.propertyInfos.end())
					classInfo.propertyInfos.push_back(propertyInfo);
				else
				{
					PropertyInfo& existingInfo = *iterFind;
					if (existingInfo.type != propertyInfo.type || existingInfo.isStatic != propertyInfo.isStatic)
					{
						outs() << "Error: Getter and setter types for the property \"" << propertyInfo.name << "\" don't match. Skipping property.\n";
						continue;
					}

					if (!propertyInfo.getter.empty())
					{
						existingInfo.getter = propertyInfo.getter;

						// Prefer documentation from setter, but use getter if no other available
						if (existingInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
					else
					{
						existingInfo.setter = propertyInfo.setter;
						existingInfo.style = propertyInfo.style; // Always prefer style flags from the setter

						if (!propertyInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
				}
			}
		}
	}

	// Generate meta-data about base classes
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			if (classInfo.baseClass.empty())
				continue;

			bool isEditor = hasAPIBED(classInfo.api);
			ClassInfo* baseClassInfo = findClassInfo(classInfo.baseClass, isEditor);
			if (baseClassInfo == nullptr)
			{
				assert(false);
				continue;
			}

			baseClassInfo->flags |= (int)ClassFlags::IsBase;
			baseClassLookup[baseClassInfo->name].childClasses.push_back(classInfo.name);
		}
	}

	// Properly generate enum default values
	auto parseDefaultValue = [&](VarInfo& paramInfo)
	{
		if (paramInfo.defaultValue.empty())
			return;

		TypeMappingInformation typeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

		if (typeInfo.TypeCategory != ::TypeCategory::Enum)
			return;

		int enumIdx = atoi(paramInfo.defaultValue.c_str());
		EnumInfo* enumInfo = findEnumInfo(paramInfo.typeName);
		if(enumInfo == nullptr)
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		auto iterFind = enumInfo->entries.find(enumIdx);
		if(iterFind == enumInfo->entries.end())
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		paramInfo.defaultValue = enumInfo->scriptName + "." + iterFind->second.ScriptName;
	};

	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
				parseDefaultValue(fieldInfo);

			for (auto& ctorInfo : structInfo.ctors)
			{
				for (auto& paramInfo : ctorInfo.params)
					parseDefaultValue(paramInfo);
			}
		}
	}

	// Find structs requiring special conversion
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				TypeMappingInformation typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

				if(isArrayOrVector(fieldInfo.flags) || !(typeInfo.TypeCategory == ::TypeCategory::Primitive || typeInfo.TypeCategory == ::TypeCategory::Enum))
				{
					structInfo.requiresInterop = true;
					break;
				}
			}

			if (structInfo.requiresInterop)
				structInfo.interopName = getStructInteropType(structInfo.name);
			else
				structInfo.interopName = structInfo.name;
		}
	}

	// Mark parameters referencing complex structs and base types
	for (auto& fileInfo : outputFileInfos)
	{
		auto markComplexType = [](const std::string& type, int& flags)
		{
			TypeMappingInformation typeInfo = getTypeInfo(type, flags);
			if (typeInfo.TypeCategory != ::TypeCategory::Struct)
				return;

			StructInfo* structInfo = findStructInfo(type);
			if (structInfo != nullptr && structInfo->requiresInterop)
				flags |= (int)TypeFlags::ComplexStruct;
		};

		auto markBaseType = [&findClassInfo](const std::string& type, int& flags)
		{
			TypeMappingInformation typeInfo = getTypeInfo(type, flags);
			if (typeInfo.TypeCategory != ::TypeCategory::Class && typeInfo.TypeCategory != ::TypeCategory::ReflectableClass &&
				typeInfo.TypeCategory != ::TypeCategory::GUIElement && !isHandleType(typeInfo.TypeCategory))
				return;

			ClassInfo* classInfo = findClassInfo(type, false);
			if (classInfo != nullptr)
			{
				bool isBase = (classInfo->flags & (int)ClassFlags::IsBase) != 0;
				if (isBase)
					flags |= (int)TypeFlags::ReferencesBase;
			}
		};

		auto markParam = [&markComplexType,&markBaseType](VarInfo& paramInfo)
		{
			markComplexType(paramInfo.typeName, paramInfo.flags);
			markBaseType(paramInfo.typeName, paramInfo.flags);
		};

		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					markParam(paramInfo);

				if (methodInfo.returnInfo.typeName.size() != 0)
				{
					markComplexType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
					markBaseType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
				}
			}

			for (auto& eventInfo : classInfo.eventInfos)
			{
				for (auto& paramInfo : eventInfo.paramInfos)
					markParam(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					markParam(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				markComplexType(fieldInfo.typeName, fieldInfo.flags);
				markParam(fieldInfo);
			}
		}
	}

	// Generate referenced includes
	{
		for (auto& fileInfo : outputFileInfos)
		{
			IncludesInfo includesInfo;
			for (auto& classInfo : fileInfo.second.classInfos)
				GatherIncludes(classInfo, includesInfo);

			for (auto& structInfo : fileInfo.second.structInfos)
				GatherIncludes(structInfo, includesInfo);

			// Needed for all .h files
			if (!fileInfo.second.inEditor)
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEnginePrerequisites.h");
			else
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEditorPrerequisites.h");

			// Needed for all .cpp files
			fileInfo.second.referencedSourceIncludes.push_back("BsScript" + fileInfo.first + ".generated.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoMethod.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoClass.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoUtil.h");

			for (auto& classInfo : fileInfo.second.classInfos)
			{
				TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

				fileInfo.second.forwardDeclarations.insert({ classInfo.ns, classInfo.cleanName, isStruct(classInfo.flags), classInfo.templParams });

				if (typeInfo.TypeCategory == ::TypeCategory::Resource)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptResource.h");
				else if (typeInfo.TypeCategory == ::TypeCategory::Component)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptComponent.h");
				else if (typeInfo.TypeCategory == ::TypeCategory::SceneObject)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptSceneObject.h");
				else if (typeInfo.TypeCategory == ::TypeCategory::GUIElement)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/GUI/BsScriptGUIElement.h");
				else if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptReflectable.h");
				else // Class
					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");

				if (!classInfo.baseClass.empty())
				{
					TypeMappingInformation& baseTypeInfo = NativeToScriptTypeMap[classInfo.baseClass];

					if(hasAPIBED(classInfo.api))
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.EditorInteropFile);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.InteropFile);
				}

				if (typeInfo.TypeCategory != ::TypeCategory::ReflectableClass && classInfo.templParams.empty())
					fileInfo.second.referencedSourceIncludes.push_back(typeInfo.NativeFile);
				else
				{
					// Templated classes need to be included in header, so the linker doesn't instantiate them multiple times for different libraries
					// (in case template is exported).
					// Reflectable classes need to be included in the header because they provide a getInternal<T>() method
					// which requires information about T.
					fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.NativeFile);
				}
			}

			for(auto& structInfo : fileInfo.second.structInfos)
			{
				TypeMappingInformation& typeInfo = NativeToScriptTypeMap[structInfo.name];

				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");
				fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.NativeFile);
			}

			if(includesInfo.requiresResourceManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptResourceManager.h");

			if (includesInfo.requiresRRef)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptRRefBase.h");

			if (includesInfo.requiresAsyncOp)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptAsyncOp.h");

			if(includesInfo.requiresGameObjectManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptGameObjectManager.h");

			if(includesInfo.requiresRTTI)
				fileInfo.second.referencedSourceIncludes.push_back("Reflection/BsRTTIType.h");

			for (auto& entry : includesInfo.includes)
			{
				uint32_t originFlags = entry.second.originIncludeFlags;
				uint32_t interopFlags = entry.second.interopIncludeFlags;

				if (originFlags != 0)
				{
					std::string include = entry.second.typeInfo.NativeFile;

					if ((originFlags & IT_FWD) != 0)
						fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.NativeNamespace, entry.second.typeName, entry.second.isStruct });

					if((originFlags & IT_IMPL) != 0)
						fileInfo.second.referencedSourceIncludes.push_back(include);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(include);
				}

				if (interopFlags != 0)
				{
					std::string include;
					if(entry.second.isEditor)
						include = entry.second.typeInfo.EditorInteropFile;
					else
						include = entry.second.typeInfo.InteropFile;

					if ((interopFlags & IT_FWD) != 0)
					{
						if(entry.second.isEditor)
							fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.NativeNamespace, entry.second.typeName, false });
					}

					if(!include.empty())
					{
						if ((interopFlags & IT_IMPL) != 0)
							fileInfo.second.referencedSourceIncludes.push_back(include);
						else
							fileInfo.second.referencedHeaderIncludes.push_back(include);
					}
				}
			}

			for (auto& entry : includesInfo.fwdDecls)
				fileInfo.second.forwardDeclarations.insert(entry.second);
		}
	}

	// Generate overloads for unsupported default parameters
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			std::vector<MethodInfo> newMethodInfos;
			for (auto& methodInfo : classInfo.methodInfos)
				PostProcessDefaultParameters(methodInfo, newMethodInfos);

			for (auto& methodInfo : newMethodInfos)
				classInfo.methodInfos.push_back(methodInfo);

			std::vector<MethodInfo> newCtorInfos;
			for (auto& ctorInfo : classInfo.ctorInfos)
				PostProcessDefaultParameters(ctorInfo, newCtorInfos);

			for (auto& ctorInfo : newCtorInfos)
				classInfo.ctorInfos.push_back(ctorInfo);
		}
	}
}

void ParserUtility::PostProcessDefaultParameters(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos)
{
	int firstDefaultParam = -1;
	int lastInvalidParam = -1;
	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValue.empty())
		{
			firstDefaultParam = i;
			break;
		}
	}

	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValueType.empty() && !isFlagsEnum(param.flags))
			lastInvalidParam = i;
	}

	// Nothing to handle
	if (lastInvalidParam == -1)
		return;

	// Mark any non-complex default params as complex, so the generator doesn't generate them (since default arguments
	// must follow them, which they can't because at least one is complex)
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];

		if (param.defaultValueType.empty())
			param.defaultValueType = "null";
	}

	// Generate a method for each default param
	for (int i = lastInvalidParam; i >= firstDefaultParam; i--)
	{
		MethodInfo copyMethodInfo = methodInfo;

		// Clear all param default values
		for (int j = firstDefaultParam; j < i; j++)
		{
			VarInfo& param = copyMethodInfo.paramInfos[j];
			param.defaultValue = "";
			param.defaultValueType = "";
		}

		// Erase docs for the params we'll skip during generation
		CommentEntry& docs = copyMethodInfo.documentation;
		for (int j = i; j <= lastInvalidParam; j++)
		{
			const std::string& paramName = copyMethodInfo.paramInfos[j].name;

			for (auto iter = docs.params.begin(); iter != docs.params.end();)
			{
				if (iter->Name == paramName)
					iter = docs.params.erase(iter);
				else
					++iter;
			}
		}

		copyMethodInfo.flags |= (int)MethodFlags::CSOnly;
		newMethodInfos.push_back(copyMethodInfo);
	}

	// Clear default params from this method
	for (int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];
		param.defaultValue = "";
		param.defaultValueType = "";
	}
}

void ParserUtility::GatherIncludes(const std::string& typeName, int flags, bool isEditor, IncludesInfo& output)
{
	TypeMappingInformation typeInfo = getTypeInfo(typeName, flags);
	if (typeInfo.TypeCategory == ::TypeCategory::Class || typeInfo.TypeCategory == ::TypeCategory::ReflectableClass ||
		typeInfo.TypeCategory == ::TypeCategory::Struct || typeInfo.TypeCategory == ::TypeCategory::Component ||
		typeInfo.TypeCategory == ::TypeCategory::SceneObject || typeInfo.TypeCategory == ::TypeCategory::Resource ||
		typeInfo.TypeCategory == ::TypeCategory::Enum)
	{
		auto iterFind = output.includes.find(typeName);
		if (iterFind == output.includes.end())
		{
			uint32_t sourceIncludeType = 0;
			uint32_t interopIncludeType = typeInfo.TypeCategory != ::TypeCategory::Enum ? IT_IMPL : 0;
			bool isStruct = false;

			if (getPassAsResourceRef(flags))
			{
				sourceIncludeType = IT_IMPL;
				interopIncludeType = 0;
			}

			if (typeInfo.TypeCategory == ::TypeCategory::Struct && !isComplexStruct(flags))
			{
				sourceIncludeType = IT_HEADER;
				isStruct = true;
			}

			// If enum or passed by value we need to include the header for the source type
			if (typeInfo.TypeCategory == ::TypeCategory::Enum || isSrcValue(flags))
				sourceIncludeType = IT_HEADER;

			// If a class is being passed by reference or a raw pointer then we need the declaration because we perform
			// assignment copy
			if (isClassType(typeInfo.TypeCategory) && !isSrcSPtr(flags))
				sourceIncludeType = IT_HEADER;

			output.includes[typeName] = IncludeInfo(typeName, typeInfo, sourceIncludeType, interopIncludeType, isStruct, isEditor);
		}

		if (isClassType(typeInfo.TypeCategory))
		{
			bool isBase = isBaseParam(flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}
	}

	if (typeInfo.TypeCategory == ::TypeCategory::Struct && isComplexStruct(flags))
		output.fwdDecls[typeName] = { typeInfo.NativeNamespace, getStructInteropType(typeName), true };

	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		output.requiresResourceManager = true;

		if (getPassAsResourceRef(flags))
			output.requiresRRef = true;
	}
	else if (typeInfo.TypeCategory == ::TypeCategory::Component || typeInfo.TypeCategory == ::TypeCategory::SceneObject)
		output.requiresGameObjectManager = true;

	if (getIsAsyncOp(flags))
		output.requiresAsyncOp = true;
}

void ParserUtility::GatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output)
{
	bool returnAsParameter = false;
	if (!methodInfo.returnInfo.typeName.empty())
		GatherIncludes(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags, isEditor, output);

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		GatherIncludes(I->typeName, I->flags, isEditor, output);

	if ((methodInfo.flags & (int)MethodFlags::External) != 0)
	{
		auto iterFind = output.includes.find(methodInfo.externalClass);
		if (iterFind == output.includes.end())
		{
			TypeMappingInformation typeInfo = getTypeInfo(methodInfo.externalClass, 0);
			output.includes[methodInfo.externalClass] = IncludeInfo(methodInfo.externalClass, typeInfo, IT_FWD_AND_IMPL, 0, false, isEditor);
		}
	}
}

void ParserUtility::GatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output)
{
	TypeMappingInformation fieldTypeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

	// These types never require additional includes
	if (fieldTypeInfo.TypeCategory == ::TypeCategory::Primitive || fieldTypeInfo.TypeCategory == ::TypeCategory::String ||
		fieldTypeInfo.TypeCategory == ::TypeCategory::WString || fieldTypeInfo.TypeCategory == ::TypeCategory::Path)
		return;

	// If passed by value, we needs its header in our header
	if (isSrcValue(fieldInfo.flags))
	{
		bool complexStruct = isComplexStruct(fieldInfo.flags);

		output.includes[fieldInfo.typeName] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_HEADER, complexStruct ? IT_HEADER : 0, false, isEditor);
	}

	if (fieldTypeInfo.TypeCategory == ::TypeCategory::Class || fieldTypeInfo.TypeCategory == ::TypeCategory::ReflectableClass ||
		fieldTypeInfo.TypeCategory == ::TypeCategory::Struct || fieldTypeInfo.TypeCategory == ::TypeCategory::Component ||
		fieldTypeInfo.TypeCategory == ::TypeCategory::SceneObject || fieldTypeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		bool isRRef = getPassAsResourceRef(fieldInfo.flags);

		if (!fieldTypeInfo.InteropFile.empty() || isRRef)
		{
			std::string name = "__" + fieldInfo.typeName;
			output.includes[name] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_IMPL, IT_IMPL, false, isEditor);
		}

		if (fieldTypeInfo.TypeCategory == ::TypeCategory::Resource)
		{
			output.requiresResourceManager = true;

			if (getPassAsResourceRef(fieldInfo.flags))
				output.requiresRRef = true;
		}
		else if (fieldTypeInfo.TypeCategory == ::TypeCategory::Component || fieldTypeInfo.TypeCategory == ::TypeCategory::SceneObject)
			output.requiresGameObjectManager = true;
		else if (fieldTypeInfo.TypeCategory == ::TypeCategory::Class || fieldTypeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
		{
			bool isBase = isBaseParam(fieldInfo.flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(fieldInfo.typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}

		if (getIsAsyncOp(fieldInfo.flags))
			output.requiresAsyncOp = true;
	}
}

void ParserUtility::GatherIncludes(const ClassInfo& classInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(classInfo.api);

	for (auto& methodInfo : classInfo.ctorInfos)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& methodInfo : classInfo.methodInfos)
		GatherIncludes(methodInfo, isEditor, output);

	for (auto& eventInfo : classInfo.eventInfos)
		GatherIncludes(eventInfo, isEditor, output);
}

void ParserUtility::GatherIncludes(const StructInfo& structInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(structInfo.api);

	if (structInfo.requiresInterop)
	{
		for (auto& fieldInfo : structInfo.fields)
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
