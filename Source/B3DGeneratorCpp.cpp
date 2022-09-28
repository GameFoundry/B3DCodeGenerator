#include "B3DCommon.h"
#include <chrono>

#include "B3DCommentParser.h"

std::string getInteropCppVarType(const std::string& typeName, ::TypeCategory type, int flags, bool forStruct = false)
{
	if (isArrayOrVector(flags))
	{
		if (isOutput(flags) && !forStruct)
			return "MonoArray**";
		else
			return "MonoArray*";
	}

	switch (type)
	{
	case ::TypeCategory::Primitive:
		if (isOutput(flags) && !forStruct)
			return typeName + "*";
		else
			return typeName;
	case ::TypeCategory::Enum:
		if (isFlagsEnum(flags) && forStruct)
			return "Flags<" + typeName + ">";
		else
		{
			if (isOutput(flags) && !forStruct)
				return typeName + "*";
			else
				return typeName;
		}
	case ::TypeCategory::Struct:
		if(isComplexStruct(flags))
		{
			if (forStruct)
				return getStructInteropType(typeName);
			else
				return getStructInteropType(typeName) + "*";
		}
		else
		{
			if (forStruct)
				return typeName;
			else
				return typeName + "*";
		}
	case ::TypeCategory::String:
	case ::TypeCategory::WString:
	case ::TypeCategory::Path:
		if (isOutput(flags) && !forStruct)
			return "MonoString**";
		else
			return "MonoString*";
	default: // Class, resource, component or ScriptObject
		if (isOutput(flags) && !forStruct)
			return "MonoObject**";
		else
			return "MonoObject*";
	}
}

std::string getCppVarType(const std::string& typeName, ::TypeCategory type, int flags = 0, bool assumeDefaultTypes = true)
{
	if (type == ::TypeCategory::Resource)
		return "ResourceHandle<" + typeName + ">";
	else if (type == ::TypeCategory::SceneObject || type == ::TypeCategory::Component)
		return "GameObjectHandle<" + typeName + ">";
	else if (isClassType(type))
	{
		if(assumeDefaultTypes || isSrcSPtr(flags))
			return "SPtr<" + typeName + ">";
		else
		{
			if(isSrcPointer(flags))
				return typeName + "*";
			else if(isSrcReference(flags))
				return typeName + "&";
			else
				return typeName;
		}
	}
	else if (type == ::TypeCategory::String)
		return "String";
	else if (type == ::TypeCategory::WString)
		return "WString";
	else if (type == ::TypeCategory::Path)
		return "Path";
	else if (type == ::TypeCategory::Enum && isFlagsEnum(flags))
		return "Flags<" + typeName + ">";
	else if(type == ::TypeCategory::GUIElement)
		return typeName + "*";
	else
		return typeName;
}

std::string getCSVarType(const std::string& typeName, ::TypeCategory type, int flags, bool paramPrefixes,
	bool arraySuffixes, bool forceStructAsRef, bool forSignature = false)
{
	std::stringstream output;

	if (!forSignature)
	{
		if (paramPrefixes && isOutput(flags))
			output << "out ";
		else if (forceStructAsRef && (isPlainStruct(type, flags)))
			output << "ref ";
	}

	output << typeName;

	if (arraySuffixes && isArrayOrVector(flags))
		output << "[]";

	if (forSignature)
	{
		if (paramPrefixes && isOutput(flags))
			output << "&";
		else if (forceStructAsRef && (isPlainStruct(type, flags)))
			output << "&";
	}

	return output.str();
}

std::string generateGetInternalLine(const std::string& sourceClassName, const std::string& obj, ::TypeCategory classType, int flags)
{
	bool isRRef = getPassAsResourceRef(flags);
	bool isBase = isBaseParam(flags);

	std::stringstream output;
	if (isClassType(classType))
		output << obj << "->GetInternal()";
	else if(classType == ::TypeCategory::GUIElement)
		output << "static_cast<" << sourceClassName << "*>(" << obj << "->GetGuiElement())";
	else // Must be one of the handle types
	{
		assert(isHandleType(classType));

		if (!isBase || isRRef)
		{
			if(isRRef)
				output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->GetHandle())";
			else
			{
				if(classType == ::TypeCategory::Resource && sourceClassName == "Resource")
					output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->GetGenericHandle())";
				else
					output << obj << "->GetHandle()";
			}
		}
		else
		{
			if (classType == ::TypeCategory::Resource)
				output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->GetGenericHandle())";
			else if (classType == ::TypeCategory::Component)
				output << "static_object_cast<" << sourceClassName << ">(" << obj << "->GetComponent())";
		}
	}
	
	return output.str();
}

std::string generateManagedToScriptObjectLine(const std::string& indent, const std::string& scriptType, 
	const std::string& scriptName, const std::string& name, ::TypeCategory type, int flags)
{
	bool isRRef = getPassAsResourceRef(flags);
	bool isBase = isBaseParam(flags);

	std::stringstream output;
	if (!isBase || isRRef)
	{
		output << indent << scriptType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = " << scriptType << "::ToNative(" << name << ");" << std::endl;
	}
	else
	{
		std::string scriptBaseType;
		if(type == ::TypeCategory::GUIElement)
			scriptBaseType = "ScriptGUIElementBaseTBase";
		else
			scriptBaseType = scriptType + "Base";

		output << indent << scriptBaseType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = (" << scriptBaseType << "*)" << scriptType << "::ToNative(" << name << ");" << std::endl;
	}

	return output.str();
}

std::string getAsManagedToCppArgumentPlain (const std::string& name, int flags, bool isPtr, const std::string& methodName)
{
	if (isSrcPointer(flags))
		return (isPtr ? "" : "&") + name;
	else if (isSrcReference(flags) || isSrcValue(flags))
		return (isPtr ? "*" : "") + name;
	else
		return name;
}

std::string getAsManagedToCppArgument(const std::string& name, ::TypeCategory type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ::TypeCategory::Primitive:
	case ::TypeCategory::Enum: // Input type is either value or pointer depending if output or not
		return getAsManagedToCppArgumentPlain(name, flags, isOutput(flags), methodName);
	case ::TypeCategory::Struct: // Input type is always a pointer
		if (isComplexStruct(flags))
			return getAsManagedToCppArgumentPlain(name, flags, false, methodName);
		else
			return getAsManagedToCppArgumentPlain(name, flags, true, methodName);
	case ::TypeCategory::MonoObject: // Input type is either a pointer or a pointer to pointer, depending if output or not
		{
			if (isOutput(flags))
				return "&" + name;
			else
				return name;
		}
	case ::TypeCategory::String: // Input type is always a value
	case ::TypeCategory::WString:
	case ::TypeCategory::Path:
		return getAsManagedToCppArgumentPlain(name, flags, false, methodName);
	case ::TypeCategory::GUIElement: // Input type is always a pointer
		return getAsManagedToCppArgumentPlain(name, flags, true, methodName);
	case ::TypeCategory::Component: // Input type is always a handle
	case ::TypeCategory::SceneObject:
	case ::TypeCategory::Resource:
	{
		if (isSrcRHandle(flags) || isSrcGHandle(flags))
			return name;
		else if (isSrcSPtr(flags))
			return name + ".GetInternalPtr()";
		else if (isSrcPointer(flags))
			return name + ".get()";
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "*" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ::TypeCategory::Class: // Input type is always a SPtr
	case ::TypeCategory::ReflectableClass:
	{
		assert(!isSrcRHandle(flags) && !isSrcGHandle(flags));

		if (isSrcPointer(flags))
			return name + ".get()";
		else if (isSrcSPtr(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "*" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}

	}
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getAsCppToManagedArgument(const std::string& name, ::TypeCategory type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ::TypeCategory::Primitive:
	case ::TypeCategory::Enum: // Always passed as value type, input can be either pointer or ref/value type
	{
		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ::TypeCategory::Struct: // Always passed as a pointer, input can be either pointer or ref/value type
	{
		if (isSrcPointer(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "&" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ::TypeCategory::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ::TypeCategory::String:
	case ::TypeCategory::WString:
	case ::TypeCategory::Path:
	case ::TypeCategory::Component:
	case ::TypeCategory::SceneObject:
	case ::TypeCategory::Resource:
	case ::TypeCategory::Class:
	case ::TypeCategory::ReflectableClass:
			return name;
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getAsCppToInteropArgument(const std::string& name, ::TypeCategory type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ::TypeCategory::Primitive: // Always passed as value type, input can be either pointer or ref/value type
	case ::TypeCategory::Enum:
	case ::TypeCategory::String:
	case ::TypeCategory::WString:
	case ::TypeCategory::Path:
	case ::TypeCategory::Struct:
	{
		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ::TypeCategory::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ::TypeCategory::GUIElement:
			return name;
	case ::TypeCategory::Component: // Always passed as a handle, input must be a handle
		if (!isSrcGHandle(flags))
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";

		if(getIsComponentOrActor(flags))
			return name + ".GetComponent()";

		return name;
	case ::TypeCategory::SceneObject:
	case ::TypeCategory::Resource:
	{
		if (isSrcRHandle(flags) || isSrcGHandle(flags))
			return name;
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ::TypeCategory::Class: // Always passed as a sptr, input can be a sptr, pointer, reference or value type
	case ::TypeCategory::ReflectableClass:
	{
		assert(!isSrcRHandle(flags) && !isSrcGHandle(flags));

		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcSPtr(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getScriptInteropType(const std::string& name, bool resourceRef = false)
{
	auto iterFind = NativeToScriptTypeMap.find(name);
	if (iterFind == NativeToScriptTypeMap.end())
		outs() << "Warning: Type \"" << name << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.\n";

	bool isValidInteropType = iterFind->second.TypeCategory != ::TypeCategory::Primitive &&
		iterFind->second.TypeCategory != ::TypeCategory::Enum &&
		iterFind->second.TypeCategory != ::TypeCategory::String &&
		iterFind->second.TypeCategory != ::TypeCategory::WString &&
		iterFind->second.TypeCategory != ::TypeCategory::Path;

	if (!isValidInteropType)
		outs() << "Error: Type \"" << name << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.\n";

	std::string cleanName = cleanTemplParams(name);

	if(resourceRef)
	{
		if(iterFind->second.TypeCategory != ::TypeCategory::Resource)
			outs() << "Error: Type \"" << name << "\" cannot be wrapped in a resource reference.\n";

		return "ScriptRRefBase";
	}
	
	return "Script" + cleanName;
}

MethodInfo findUnusedCtorSignature(const ClassInfo& classInfo)
{
	auto checkSignature = [](int numParams, const MethodInfo& info)
	{
		if ((int)info.paramInfos.size() != numParams)
			return true;

		for (auto& paramInfo : info.paramInfos)
		{
			if (paramInfo.typeName != "bool")
				return true;
		}

		return false;
	};

	int numBools = 1;
	while (true)
	{
		bool isSignatureValid = true;

		// Check normal constructors
		for (auto& entry : classInfo.ctorInfos)
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
			for (auto& entry : classInfo.methodInfos)
			{
				bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
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
	output.sourceName = classInfo.cleanName;
	output.scriptName = classInfo.cleanName;
	output.flags = (int)MethodFlags::Constructor;
	output.visibility = CSVisibility::Private;

	for (int i = 0; i < numBools; i++)
	{
		VarInfo paramInfo;
		paramInfo.name = "__dummy" + std::to_string(i);
		paramInfo.typeName = "bool";
		paramInfo.flags = (int)TypeFlags::Primitive;

		output.paramInfos.push_back(paramInfo);
	}

	return output;
}

bool hasParameterlessConstructor(const ClassInfo& classInfo)
{
	// Check normal constructors
	for (auto& entry : classInfo.ctorInfos)
	{
		if (entry.paramInfos.size() == 0)
			return true;
	}

	// Check external constructors
	for (auto& entry : classInfo.methodInfos)
	{
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		if (!isConstructor)
			continue;

		if (entry.paramInfos.size() == 0)
			return true;
	}

	return false;
}

std::string generateFileHeader(bool isBanshee)
{
	std::stringstream output;
	if (isBanshee)
		output << sEditorCopyrightNotice;
	else
		output << sFrameworkCopyrightNotice;

	return output.str();
}

std::string generateCppApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::Framework)
		return "#if !BS_IS_BANSHEE3D\n";
	else if(api == ApiFlags::Engine)
		return "#if BS_IS_BANSHEE3D\n";

	return "";
}

std::string generateCsApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::Framework)
		return "#if !IS_B3D\n";
	else if(api == ApiFlags::Engine)
		return "#if IS_B3D\n";

	return "";
}

std::string generateApiCheckEnd(ApiFlags api)
{
	if(api == ApiFlags::Framework || api == ApiFlags::Engine)
		return "#endif\n";

	return "";
}

std::string generateCppMethodSignature(const MethodInfo& methodInfo, const std::string& thisPtrType, const std::string& nestedName, bool isModule)
{
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.returnInfo.typeName.empty() || isCtor)
		output << "void";
	else
	{
		TypeMappingInformation returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			output << getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags);
		}
	}

	output << " ";

	if (!nestedName.empty())
		output << nestedName << "::";

	output << "Internal" << methodInfo.interopName << "(";

	if (isCtor)
	{
		output << "MonoObject* managedInstance";

		if (methodInfo.paramInfos.size() > 0)
			output << ", ";
	}
	else if (!isStatic && !isModule)
	{
		output << thisPtrType << "* thisPtr";

		if (methodInfo.paramInfos.size() > 0 || returnAsParameter)
			output << ", ";
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		TypeMappingInformation paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		output << getInteropCppVarType(I->typeName, paramTypeInfo.TypeCategory, I->flags) << " " << I->name;

		if ((I + 1) != methodInfo.paramInfos.end() || returnAsParameter)
			output << ", ";
	}

	if (returnAsParameter)
	{
		TypeMappingInformation returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);

		output << getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags) <<
			" " << "__output";
	}

	output << ")";
	return output.str();
}

std::string generateCppEventCallbackSignature(const MethodInfo& eventInfo, const std::string& nestedName, bool isModule)
{
	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;

	if ((isStatic || isModule) && nestedName.empty())
		output << "static ";

	output << "void ";
	
	if (!nestedName.empty())
		output << nestedName << "::";
	
	output << eventInfo.interopName << "(";

	int idx = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		TypeMappingInformation paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		if (!isSrcValue(I->flags) && !isOutput(I->flags))
			output << "const ";

		if (isVector(I->flags))
			output << "std::vector<";
		else if(isSmallVector(I->flags))
			output << "SmallVector<";

		output << getCppVarType(I->typeName, paramTypeInfo.TypeCategory, I->flags, false);

		if(!isSrcValue(I->flags))
		{
			if (isSrcPointer(I->flags))
				output << "*";
			else if (isSrcReference(I->flags))
				output << "&";
		}

		if(isSmallVector(I->flags))
			output << ", " << I->arraySize << ">";

		if (isVector(I->flags))
			output << ">";

		output << " p" << idx;

		if (isArray(I->flags))
			output << "[" << I->arraySize << "]";

		if ((I + 1) != eventInfo.paramInfos.end())
			output << ", ";

		idx++;
	}

	output << ")";
	return output.str();
}

std::string generateCppEventThunk(const MethodInfo& eventInfo, bool isModule)
{
	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;
	output << "\t\ttypedef void(BS_THUNKCALL *" << eventInfo.sourceName << "ThunkDef) (";
	
	if (!isStatic && !isModule)
		output << "MonoObject*, ";

	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		TypeMappingInformation paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		if (paramTypeInfo.TypeCategory == ::TypeCategory::Struct)
			output << "MonoObject* " << I-> name << ", ";
		else
			output << getInteropCppVarType(I->typeName, paramTypeInfo.TypeCategory, I->flags) << " " << I->name << ", ";
	}

	output << "MonoException**);" << std::endl;
	output << "\t\tstatic " << eventInfo.sourceName << "ThunkDef " << eventInfo.sourceName << "Thunk;" << std::endl;

	return output.str();
}

std::string generateClassNativeToScriptObjectLine(int flags, const std::string& typeName, const std::string& outputName, 
	const std::string& scriptType, const std::string& argName, bool asRef = false, const std::string& indent = "\t\t")
{
	std::stringstream output;

	auto generateCreateLine = [&output, &outputName, asRef](const std::string& scriptType, const std::string& argName, const std::string& indent)
	{
		if (asRef)
			output << indent << "MonoUtil::ReferenceCopy(" << outputName << ", " << scriptType << "::Create(" << argName << "));\n";
		else
			output << indent << outputName << " = " << scriptType << "::Create(" << argName << ");\n";
	};

	if(isBaseParam(flags))
	{
		std::vector<std::string> derivedClasses;
		getDerivedClasses(typeName, derivedClasses);

		if(!derivedClasses.empty())
		{
			output << indent << "if(" << argName << ")\n";
			output << indent << "{\n";

			output << indent << "\tif(rtti_is_of_type<" << derivedClasses[0] << ">(" << argName << "))\n";
			generateCreateLine(getScriptInteropType(derivedClasses[0]), 
				"std::static_pointer_cast<" + derivedClasses[0] + ">(" + argName + ")", indent + "\t\t");

			for(uint32_t i = 1; i < (uint32_t)derivedClasses.size(); i++)
			{
				output << indent << "\telse if(rtti_is_of_type<" << derivedClasses[i] << ">(" << argName << "))\n";
				generateCreateLine(getScriptInteropType(derivedClasses[i]),
					"std::static_pointer_cast<" + derivedClasses[i] + ">(" + argName + ")", indent + "\t\t");
			}

			output << indent << "\telse\n";
			generateCreateLine(scriptType, argName, indent + "\t\t");


			output << indent << "}\n";
			output << indent << "else\n";
			generateCreateLine(scriptType, argName, indent + "\t");

			return output.str();
		}
	}
	else
		generateCreateLine(scriptType, argName, indent);

	return output.str();
}

std::string generateNativeToScriptObjectLine(::TypeCategory type, int flags, const std::string& scriptName,
	const std::string& argName, const std::string& indent = "\t\t")
{
	std::stringstream output;

	if (type == ::TypeCategory::Resource)
	{
		if(getPassAsResourceRef(flags))
		{
			output << indent << "ScriptRRefBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::Instance().GetScriptRRef(" << argName << ");\n";
		}
		else
		{
			output << indent << "ScriptResourceBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::Instance().GetScriptResource(" << argName
				<< ", true);\n";
		}
	}
	else if (type == ::TypeCategory::Component)
	{
		output << indent << "ScriptComponentBase* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << "\t" << scriptName << " = ScriptGameObjectManager::Instance().GetBuiltinScriptComponent(" <<
			"static_object_cast<Component>(" << argName << "));\n";
	}
	else if (type == ::TypeCategory::SceneObject)
	{
		output << indent << "ScriptSceneObject* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << scriptName << " = ScriptGameObjectManager::Instance().GetOrCreateScriptSceneObject(" <<
			argName << ");\n";
	}
	else
		assert(false);

	return output.str();
}

std::string generateMethodBodyBlockForParam(const std::string& name, const VarTypeInfo& varTypeInfo,
	bool isLast, bool returnValue, std::stringstream& preCallActions, std::stringstream& postCallActions)
{
	TypeMappingInformation paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if(getIsAsyncOp(varTypeInfo.flags))
	{
		if (!isOutput(varTypeInfo.flags) && !returnValue)
		{
			outs() << "Error: AsyncOp type not supported as input parameter. \n";
			return "";
		}

		if (paramTypeInfo.TypeCategory != ::TypeCategory::ReflectableClass && paramTypeInfo.TypeCategory != ::TypeCategory::Class &&
			paramTypeInfo.TypeCategory != ::TypeCategory::Resource)
		{
			outs() << "Error: Type not supported as an AsyncOp return value. \n";
			return "";
		}

		std::string argType;
		std::string argName;
		if (!isArrayOrVector(varTypeInfo.flags))
		{
			argName = "tmp" + name;
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);

			preCallActions << "\t\tTAsyncOp<" << argType << "> " << argName << ";\n";
		}
		else
		{
			if (isVector(varTypeInfo.flags))
				argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ">";
			else if(isSmallVector(varTypeInfo.flags))
				argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
			else
				argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false);

			argName = "vec" + name;

			preCallActions << "\t\t" << argType << " " << argName;
			if (isArray(varTypeInfo.flags))
				preCallActions << "[" << varTypeInfo.arraySize << "]";
			preCallActions << ";\n";
		}

		std::string monoType;
		if(varTypeInfo.typeName != "Any")
		{
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName,
				paramTypeInfo.TypeCategory == ::TypeCategory::Resource && getPassAsResourceRef(varTypeInfo.flags));

			monoType = scriptType + "::GetMetaData()->ScriptClass";

			postCallActions << "\t\tauto convertCallback = [](const Any& returnVal)\n";
			postCallActions << "\t\t{\n";
			postCallActions << "\t\t\t" << argType << " nativeObj = any_cast<" << argType << ">(returnVal);\n";
			postCallActions << "\t\t\tMonoObject* monoObj;\n";

			if (!isArrayOrVector(varTypeInfo.flags))
			{
				if (paramTypeInfo.TypeCategory == ::TypeCategory::ReflectableClass || paramTypeInfo.TypeCategory == ::TypeCategory::Class)
					postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, "monoObj", scriptType, "nativeObj", false, "\t\t\t");
				else // Resource
				{
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, "scriptObj", "nativeObj", "\t\t\t");
					postCallActions << "\t\t\tif(scriptObj != nullptr)" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = scriptObj->GetManagedInstance();" << std::endl;
					postCallActions << "\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = nullptr;" << std::endl;
				}

			}
			else
			{
				std::string arrayName = "scriptArray";

				postCallActions << "\t\t\tint arraySize = ";
				if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
					postCallActions << "(int)" << argName << ".size()";
				else
					postCallActions << varTypeInfo.arraySize;
				postCallActions << ";\n";

				postCallActions << "\t\t\tScriptArray " << arrayName;
				postCallActions << " = " << "ScriptArray::Create<" << scriptType << ">(arraySize);" << std::endl;
				postCallActions << "\t\t\tfor(int i = 0; i < arraySize; i++)" << std::endl;
				postCallActions << "\t\t\t{" << std::endl;

				switch (paramTypeInfo.TypeCategory)
				{
				case ::TypeCategory::ReflectableClass:
				case ::TypeCategory::Class:
				{
					std::string elemName = "arrayElem" + name;

					std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags);
					std::string elemPtrName = "arrayElemPtr" + name;

					postCallActions << "\t\t\t\t" << elemPtrType << " " << elemPtrName;
					if (willBeDereferenced(varTypeInfo.flags))
					{
						postCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

						if (isSrcPointer(varTypeInfo.flags))
						{
							postCallActions << "\t\t\t\tif(nativeObj[i])\n";
							postCallActions << "\t\t\t\t\t*" << elemPtrName << " = *";
						}
						else
						{
							postCallActions << "\t\t\t\t*" << elemPtrName << " = ";
						}

						postCallActions << "nativeObj[i];\n";
					}
					else
						postCallActions << " = nativeObj[i];\n";

					postCallActions << "\t\t\t\tMonoObject* " << elemName << ";\n";
					postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName,
						scriptType, elemPtrName, false, "\t\t\t\t");

					postCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
					break;
				}
				case ::TypeCategory::Resource:
				{
					std::string scriptName = "scriptObj";

					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, "nativeObj[i]", "\t\t\t\t");
					postCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
					postCallActions << "\t\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
				}
				break;
				default:
					outs() << "Error: Type not supported as an AsyncOp return value. \n";
					break;
				}

				postCallActions << "\t\t\t}" << std::endl;
				postCallActions << "\t\t\tmonoObj = " << arrayName << ".GetInternal();" << std::endl;
			}

			postCallActions << "\t\t\treturn monoObj;\n";
			postCallActions << "\t\t};\n";
			postCallActions << "\n;";
		}
		else
			postCallActions << "\t\tauto convertCallback = nullptr;\n";

		if (returnValue)
			postCallActions << "\t\t" << name << " = " << "ScriptAsyncOpBase::Create(" << argName << ", convertCallback, " << monoType << ");\n";
		else
			postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ", " << "ScriptAsyncOpBase::Create(" << argName << ", convertCallback, " << monoType << "));\n";

		return argName;
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string argName;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::Enum:
		case ::TypeCategory::Struct:
			if (returnValue)
			{
				argName = "tmp" + name;

				if(isFlagsEnum(varTypeInfo.flags))
					preCallActions << "\t\tFlags<" << varTypeInfo.typeName << "> " << argName << ";" << std::endl;
				else
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

				if (paramTypeInfo.TypeCategory == ::TypeCategory::Struct)
				{
					if(isComplexStruct(varTypeInfo.flags))
					{
						std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

						postCallActions << "\t\t" << getStructInteropType(varTypeInfo.typeName) << " interop" << name << ";\n";
						postCallActions << "\t\tinterop" << name << " = " << scriptType << "::ToInterop(" << argName << ");\n";

						postCallActions << "\t\tMonoUtil::ValueCopy(" << name << ", ";
						postCallActions << "&interop" << name << ", ";
						postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClassInternal());\n";
					}
					else
						postCallActions << "\t\t*" << name << " = " << argName << ";" << std::endl;
				}
				else if(isFlagsEnum(varTypeInfo.flags))
					postCallActions << "\t\t" << name << " = (" << varTypeInfo.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				else
					postCallActions << "\t\t" << name << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				if(paramTypeInfo.TypeCategory == ::TypeCategory::Struct && isComplexStruct(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

					postCallActions << "\t\t" << getStructInteropType(varTypeInfo.typeName) << " interop" << name << ";\n";
					postCallActions << "\t\tinterop" << name << " = " << scriptType << "::ToInterop(" << argName << ");\n";

					postCallActions << "\t\tMonoUtil::ValueCopy(" << name << ", ";
					postCallActions << "&interop" << name << ", ";
					postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClassInternal());\n";
				}
				else if (isFlagsEnum(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\tFlags<" << varTypeInfo.typeName << "> " << argName << ";" << std::endl;

					postCallActions << "\t\t*" << name << " = (" << varTypeInfo.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				}
				else
					argName = name;
			}
			else
			{
				if(paramTypeInfo.TypeCategory == ::TypeCategory::Struct && isComplexStruct(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = getScriptInteropType(varTypeInfo.typeName);
					preCallActions << "\t\t" << argName << " = " << scriptType << "::FromInterop(*" << name << ");" << std::endl;
				}
				else
					argName = name;
			}

			break;
		case ::TypeCategory::String:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::StringToMono(" << argName << ");" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ",  (MonoObject*)MonoUtil::StringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToString(" << name << ");" << std::endl;
		}
		break;
		case ::TypeCategory::Path:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tPath " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::StringToMono(" << argName << ".ToString());" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ",  (MonoObject*)MonoUtil::StringToMono(" << argName << ".ToString()));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToString(" << name << ");" << std::endl;
		}
		break;
		case ::TypeCategory::WString:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tWString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::WstringToMono(" << argName << ");" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ", (MonoObject*)MonoUtil::WstringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::MonoToWString(" << name << ");" << std::endl;
		}
		break;
		case ::TypeCategory::MonoObject:
		{
			argName = "tmp" + name;
			
			if (returnValue)
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\t" << name << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ", " << argName << ");" << std::endl;
			}
			else
			{
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
			}
		}
		break;
		case ::TypeCategory::GUIElement:
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName << ";\n";
			if(returnValue || isOutput(varTypeInfo.flags))
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
			else
			{
				std::string scriptName = "script" + name;

				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, 
					paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, 
					paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";" << std::endl;
			}
		}
			break;
		case ::TypeCategory::Class:
		case ::TypeCategory::ReflectableClass:
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName;
			if ((returnValue || isOutput(varTypeInfo.flags)) && willBeDereferenced(varTypeInfo.flags))
				preCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">()";

			preCallActions << ";\n";

			if (returnValue)
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, name, scriptType, argName);
			else if (isOutput(varTypeInfo.flags))
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, name, scriptType, argName, true);
			else
			{
				std::string scriptName = "script" + name;
				
				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, 
					paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, 
					paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";" << std::endl;
			}
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);

			preCallActions << "\t\t" << tmpType << " " << argName << ";" << std::endl;

			std::string scriptName = "script" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));

			if (returnValue)
			{
				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t" << name << " = " << scriptName << "->GetManagedInstance();" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t" << name << " = nullptr;" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\tMonoUtil::ReferenceCopy(" << name << ", " << scriptName << "->GetManagedInstance());" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t*" << name << " = nullptr;" << std::endl;
			}
			else
			{
				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";" << std::endl;
			}
		}
		break;
		}

		return argName;
	}
	else
	{
		std::string entryType;
		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::String:
		case ::TypeCategory::WString:
		case ::TypeCategory::Path:
		case ::TypeCategory::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ::TypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argType;
		
		if (isVector(varTypeInfo.flags))
			argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ">";
		else if(isSmallVector(varTypeInfo.flags))
			argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
		else
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false);

		std::string argName = "vec" + name;

		preCallActions << "\t\t" << argType << " " << argName;
		if (isArray(varTypeInfo.flags))
			preCallActions << "[" << varTypeInfo.arraySize << "]";
		preCallActions << ";\n";

		if (!isOutput(varTypeInfo.flags) && !returnValue)
		{
			std::string arrayName = "array" + name;

			preCallActions << "\t\tif(" << name << " != nullptr)\n";
			preCallActions << "\t\t{\n";

			preCallActions << "\t\t\tScriptArray " << arrayName << "(" << name << ");" << std::endl;

			if(isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preCallActions << "\t\t\t" << argName << ".resize(" << arrayName << ".Size());" << std::endl;

			preCallActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".Size(); i++)" << std::endl;
			preCallActions << "\t\t\t{" << std::endl;

			switch (paramTypeInfo.TypeCategory)
			{
			case ::TypeCategory::Primitive:
			case ::TypeCategory::String:
			case ::TypeCategory::WString:
			case ::TypeCategory::Path:
				preCallActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".Get<" << entryType << ">(i);" << std::endl;
				break;
			case ::TypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ::TypeCategory::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

				preCallActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".Get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ::TypeCategory::Struct:

				preCallActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(varTypeInfo.flags))
				{
					preCallActions << entryType << "::FromInterop(";
					preCallActions << arrayName << ".Get<" << getStructInteropType(varTypeInfo.typeName) << ">(i)";
					preCallActions << ")";
				}
				else
					preCallActions << arrayName << ".Get<" << varTypeInfo.typeName << ">(i)";

				preCallActions << ";\n";

				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;

				preCallActions << generateManagedToScriptObjectLine("\t\t\t\t", entryType, scriptName, arrayName + ".Get<MonoObject*>(i)", paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preCallActions << "\t\t\t\t{\n";

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preCallActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";\n";

				if(paramTypeInfo.TypeCategory == ::TypeCategory::Class || paramTypeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
				{
					if(isSrcPointer(varTypeInfo.flags))
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".Get();\n";
					else if((isSrcReference(varTypeInfo.flags) || isSrcValue(varTypeInfo.flags)) && !isSrcSPtr(varTypeInfo.flags))
					{
						preCallActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preCallActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preCallActions << "\t\t\t\t}\n";
			}
			break;
			}

			preCallActions << "\t\t\t}" << std::endl;

			if (!isLast)
				preCallActions << std::endl;

			preCallActions << "\t\t}\n";
		}
		else
		{
			std::string arrayName = "array" + name;

			postCallActions << "\t\tint arraySize" << name << " = ";
			if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				postCallActions << "(int)" << argName << ".size()";
			else
				postCallActions << varTypeInfo.arraySize;
			postCallActions << ";\n";

			postCallActions << "\t\tScriptArray " << arrayName;
			postCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			postCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			postCallActions << "\t\t{" << std::endl;

			switch (paramTypeInfo.TypeCategory)
			{
			case ::TypeCategory::Primitive:
			case ::TypeCategory::String:
			case ::TypeCategory::WString:
			case ::TypeCategory::Path:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << argName << "[i]);" << std::endl;
				break;
			case ::TypeCategory::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

				if(isFlagsEnum(varTypeInfo.flags))
					postCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")(uint32_t)" << argName << "[i]);" << std::endl;
				else
					postCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")" << argName << "[i]);" << std::endl;
				break;
			}
			case ::TypeCategory::Struct:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, ";

				if(isComplexStruct(varTypeInfo.flags))
					postCallActions << entryType << "::ToInterop(";

				postCallActions << argName << "[i]";

				if (isComplexStruct(varTypeInfo.flags))
					postCallActions << ")";

				postCallActions << ");\n";

				break;
			case ::TypeCategory::MonoObject:
				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << argName << "[i]);" << std::endl;
				break;
			case ::TypeCategory::Class:
			case ::TypeCategory::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				postCallActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(willBeDereferenced(varTypeInfo.flags))
				{
					postCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

					if (isSrcPointer(varTypeInfo.flags))
					{
						postCallActions << "\t\t\tif(" << argName << "[i])\n";
						postCallActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						postCallActions << "\t\t\t*" << elemPtrName << " = ";
					}

					postCallActions << argName << "[i];\n";
				}
				else
					postCallActions << " = " << argName << "[i];\n";

				postCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName, 
					entryType, elemPtrName, false, "\t\t\t");

				postCallActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
				break;
			}
			case ::TypeCategory::GUIElement:
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + name;

				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, argName + "[i]", "\t\t\t");
				postCallActions << "\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
				postCallActions << "\t\t\telse" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
			}
			break;
			}

			postCallActions << "\t\t}" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = " << arrayName << ".GetInternal();" << std::endl;
			else
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << name << ", (MonoObject*)" << arrayName << ".GetInternal());" << std::endl;
		}

		return argName;
	}
}

std::string generateFieldConvertBlock(const std::string& name, const VarTypeInfo& varTypeInfo, bool toInterop, std::stringstream& preActions)
{
	TypeMappingInformation paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if (getIsAsyncOp(varTypeInfo.flags))
	{
		outs() << "Error: AsyncOp type not supported as a struct field. \n";
		return "";
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string arg;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::Enum:
			arg = "value." + name;
			break;
		case ::TypeCategory::Struct:
			if(isComplexStruct(varTypeInfo.flags))
			{
				std::string interopType = getStructInteropType(varTypeInfo.typeName);
				std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

				arg = "tmp" + name;
				if(toInterop)
				{
					preActions << "\t\t" << interopType << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::ToInterop(value." << name << ");" << std::endl;
				}
				else
				{
					preActions << "\t\t" << varTypeInfo.typeName << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::FromInterop(value." << name << ");" << std::endl;
				}
			}
			else
				arg = "value." + name;
			break;
		case ::TypeCategory::String:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::StringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::TypeCategory::WString:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::WstringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tWString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToWString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::TypeCategory::Path:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::StringToMono(value." << name << ".ToString());" << std::endl;
			}
			else
			{
				preActions << "\t\tPath " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::MonoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ::TypeCategory::MonoObject:
		{
			arg = "tmp" + name;

			preActions << "\t\tMonoObject* " << arg << ";" << std::endl;
			preActions << "\t\t" << arg << " = " << name << ";" << std::endl;
		}
		break;
		case ::TypeCategory::GUIElement:
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			if(!toInterop)
			{
				if(isSrcPointer(varTypeInfo.flags))
				{
					std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
					preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

					std::string scriptName = "script" + name;
					preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, 
						paramTypeInfo.TypeCategory, varTypeInfo.flags);
					preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
					preActions << "\t\t\t" << arg << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName,
						paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";" << std::endl;
				}
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		case ::TypeCategory::Class:
		case ::TypeCategory::ReflectableClass:
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			if(toInterop)
			{
				preActions << "\t\tMonoObject* " << arg << ";\n";

				// Need to copy by value
				if(isSrcValue(varTypeInfo.flags) || isSrcPointer(varTypeInfo.flags))
				{
					std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
					preActions << "\t\t" << tmpType << " " << arg << "copy;\n";

					// Note: Assuming a copy constructor exists
					if (isSrcPointer(varTypeInfo.flags))
					{
						preActions << "\t\tif(value." << name << " != nullptr)\n";
						preActions << "\t\t\t" << arg << "copy = bs_shared_ptr_new<" << varTypeInfo.typeName << ">(*value." << name << ");\n";
					}
					else
						preActions << "\t\t" << arg << "copy = bs_shared_ptr_new<" << varTypeInfo.typeName << ">(value." << name << ");\n";

					preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, arg, scriptType, arg + "copy");
				}
				else if(isSrcSPtr(varTypeInfo.flags))
					preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, arg, scriptType, "value." + name);
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
			else
			{
				std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

				std::string scriptName = "script" + name;
				preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preActions << "\t\t\t" << arg << " = " << scriptName << "->GetInternal();" << std::endl;

				// Cast to the source type from SPtr
				if (isSrcValue(varTypeInfo.flags))
				{
					preActions << "\t\tif(" << arg << " != nullptr)" << std::endl;
					arg = "*" + arg;
				}
				else if (isSrcPointer(varTypeInfo.flags))
					arg = arg + ".get()";
				else if(!isSrcSPtr(varTypeInfo.flags))
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		default: // Some resource or game object type
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			std::string scriptName = "script" + name;

			if(toInterop)
			{
				std::string argName;
				
				if(!getIsComponentOrActor(varTypeInfo.flags))
					argName = "value." + name;
				else
					argName = "value." + name + ".GetComponent()";

				preActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, argName);

				preActions << "\t\tMonoObject* " << arg << ";\n";
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << scriptName << "->GetManagedInstance();" << std::endl;
				preActions << "\t\telse\n";
				preActions << "\t\t\t" << arg << " = nullptr;\n";
			}
			else
			{
				std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;
				
				preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";" << std::endl;
			}

			if(!isSrcGHandle(varTypeInfo.flags) && !isSrcRHandle(varTypeInfo.flags))
				outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
		}
		break;
		}

		return arg;
	}
	else
	{
		std::string entryType;
		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::String:
		case ::TypeCategory::WString:
		case ::TypeCategory::Path:
		case ::TypeCategory::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ::TypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argType;
		if(isVector(varTypeInfo.flags))
			argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ">";
		else if(isSmallVector(varTypeInfo.flags))
			argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
		else
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags, false);

		std::string argName = "vec" + name;

		if (!toInterop)
		{
			std::string arrayName = "array" + name;
			preActions << "\t\t" << argType << " " << argName;
			if (isArray(varTypeInfo.flags))
				preActions << "[" << varTypeInfo.arraySize << "]";
			preActions << ";" << std::endl;

			preActions << "\t\tif(value." << name << " != nullptr)\n";
			preActions << "\t\t{\n";
			preActions << "\t\t\tScriptArray " << arrayName << "(value." << name << ");" << std::endl;

			if(isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preActions << "\t\t\t" << argName << ".resize(" << arrayName << ".Size());" << std::endl;

			preActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".Size(); i++)" << std::endl;
			preActions << "\t\t\t{" << std::endl;

			switch (paramTypeInfo.TypeCategory)
			{
			case ::TypeCategory::Primitive:
			case ::TypeCategory::String:
			case ::TypeCategory::WString:
			case ::TypeCategory::Path:
				preActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".Get<" << entryType << ">(i);" << std::endl;
				break;
			case ::TypeCategory::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ::TypeCategory::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

				preActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ::TypeCategory::Struct:
				preActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(varTypeInfo.flags))
				{
					preActions << entryType << "::FromInterop(";
					preActions << arrayName << ".Get<" << getStructInteropType(varTypeInfo.typeName) << ">(i)";
					preActions << ")";
				}
				else
					preActions << arrayName << ".Get<" << varTypeInfo.typeName << ">(i)";

				preActions << ";\n";
				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;
				preActions << generateManagedToScriptObjectLine("\t\t\t\t", entryType, scriptName, arrayName + ".Get<MonoObject*>(i)", paramTypeInfo.TypeCategory, varTypeInfo.flags);
				
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t{\n";

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.TypeCategory, varTypeInfo.flags) << ";\n";

				if(paramTypeInfo.TypeCategory == ::TypeCategory::Class || paramTypeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
				{
					if(isSrcPointer(varTypeInfo.flags))
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".get();\n";
					else if((isSrcReference(varTypeInfo.flags) || isSrcValue(varTypeInfo.flags)) && !isSrcSPtr(varTypeInfo.flags))
					{
						preActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preActions << "\t\t\t\t}\n";
			}
			break;
			}

			preActions << "\t\t\t}" << std::endl;
			preActions << "\t\t}\n";
		}
		else
		{
			preActions << "\t\tint arraySize" << name << " = ";
			if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preActions << "(int)value." << name << ".size()";
			else
				preActions << varTypeInfo.arraySize;
			preActions << ";\n";

			preActions << "\t\tMonoArray* " << argName << ";" << std::endl;

			std::string arrayName = "array" + name;
			preActions << "\t\tScriptArray " << arrayName;
			preActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			preActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			preActions << "\t\t{" << std::endl;

			switch (paramTypeInfo.TypeCategory)
			{
			case ::TypeCategory::Primitive:
			case ::TypeCategory::String:
			case ::TypeCategory::WString:
			case ::TypeCategory::Path:
				preActions << "\t\t\t" << arrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::TypeCategory::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

				preActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")value." << name << "[i]);" << std::endl;
				break;
			}
			case ::TypeCategory::Struct:
				preActions << "\t\t\t" << arrayName << ".Set(i, ";

				if(isComplexStruct(varTypeInfo.flags))
					preActions << entryType << "::ToInterop(";

				preActions << "value." << name << "[i]";

				if (isComplexStruct(varTypeInfo.flags))
					preActions << ")";

				preActions << ");\n";
				break;
			case ::TypeCategory::MonoObject:
				preActions << "\t\t\t" << arrayName << ".Set(i, value." << name << "[i]);" << std::endl;
				break;
			case ::TypeCategory::Class:
			case ::TypeCategory::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.TypeCategory, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(willBeDereferenced(varTypeInfo.flags))
				{
					preActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

					if (isSrcPointer(varTypeInfo.flags))
					{
						preActions << "\t\t\tif(value." << name << "[i])\n";
						preActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						preActions << "\t\t\t*" << elemPtrName << " = ";
					}

					preActions << "value." << name << "[i];\n";
				}
				else
					preActions << " = value." << name << "[i];\n";

				preActions << "\t\t\tMonoObject* " << elemName << ";\n";
				preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName, 
					entryType, elemPtrName, false, "\t\t\t");

				preActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
			}
			break;
			case ::TypeCategory::GUIElement:
				// Unsupported as output
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + name;

				preActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, "value." + name + "[i]", "\t\t\t");
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
				preActions << "\t\t\telse\n";
				preActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
			}
			break;
			}

			preActions << "\t\t}" << std::endl;
			preActions << "\t\t" << argName << " = " << arrayName << ".GetInternal();" << std::endl;
		}

		return argName;
	}
}

std::string generateEventCallbackBodyBlockForParam(const std::string& name, const VarTypeInfo& varTypeInfo, std::stringstream& preCallActions)
{
	TypeMappingInformation paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if (getIsAsyncOp(varTypeInfo.flags))
	{
		outs() << "Error: AsyncOp type not supported as an event callback parameter. \n";
		return "";
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string argName;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
			argName = name;
			break;
		case ::TypeCategory::Enum:
			if(isFlagsEnum(varTypeInfo.flags))
			{
				argName = "tmp" + name;
				preCallActions << "\t\t" << varTypeInfo.typeName << argName << ";" << std::endl;
				preCallActions << "\t\t" << argName << " = (" << varTypeInfo.typeName << ")(uint32_t)" << name << ";" << std::endl;
			}
			else
				argName = name;
			break;
		case ::TypeCategory::Struct:
			{
				argName = "tmp" + name;

				std::string scriptType = getScriptInteropType(varTypeInfo.typeName);
				preCallActions << "\t\tMonoObject* " << argName << ";\n";

				if(isComplexStruct(varTypeInfo.flags))
				{
					std::string interopName = "interop" + name;
					std::string interopType = getStructInteropType(varTypeInfo.typeName);
					
					preCallActions << "\t\t" << interopType << " " << interopName << ";" << std::endl;
					preCallActions << "\t\t" << interopName << " = " << scriptType << "::ToInterop(" << name << ");" << std::endl;
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << interopName << ");\n";
				}
				else
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << name << ");\n";
			}

			break;
		case ::TypeCategory::String:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::StringToMono(" << name << ");" << std::endl;
		}
		break;
		case ::TypeCategory::WString:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::WstringToMono(" << name << ");" << std::endl;
		}
		break;
		case ::TypeCategory::Path:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::StringToMono(" << name << ".ToString());" << std::endl;
		}
		break;
		case ::TypeCategory::MonoObject:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << " = " << name << ";\n";
		}
		break;
		case ::TypeCategory::Class:
		case ::TypeCategory::ReflectableClass:
		{
			argName = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\tMonoObject* " << argName << ";\n";
			preCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, argName, scriptType, name);
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;

			std::string scriptName = "script" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));

			preCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, name);
			preCallActions << "\t\tif(" << scriptName << " != nullptr)\n";
			preCallActions << "\t\t\t" << argName << " = " << scriptName << "->GetManagedInstance();" << std::endl;
			preCallActions << "\t\telse\n";
			preCallActions << "\t\t\t" << argName << " = nullptr;\n";
		}
		break;
		}

		return argName;
	}
	else
	{
		std::string entryType;
		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::String:
		case ::TypeCategory::WString:
		case ::TypeCategory::Path:
		case ::TypeCategory::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ::TypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argName = "vec" + name;
		preCallActions << "\t\tMonoArray* " << argName << ";" << std::endl;

		preCallActions << "\t\tint arraySize" << name << " = ";
		if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
			preCallActions << "(int)value." << name << ".size()";
		else
			preCallActions << varTypeInfo.arraySize;
		preCallActions << ";\n";

		std::string arrayName = "array" + name;
		preCallActions << "\t\tScriptArray " << arrayName;
		preCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
		preCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
		preCallActions << "\t\t{" << std::endl;

		switch (paramTypeInfo.TypeCategory)
		{
		case ::TypeCategory::Primitive:
		case ::TypeCategory::String:
		case ::TypeCategory::WString:
		case ::TypeCategory::Path:
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ::TypeCategory::Enum:
		{
			std::string enumType;
			mapBuiltinTypeToCppType(paramTypeInfo.EnumUnderlyingType, enumType);

			if(isFlagsEnum(varTypeInfo.flags))
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")(uint32_t)" << name << "[i]);" << std::endl;
			else
				preCallActions << "\t\t\t" << arrayName << ".Set(i, (" << enumType << ")" << name << "[i]);" << std::endl;
			break;
		}
		case ::TypeCategory::Struct:
			preCallActions << "\t\t\t" << arrayName << ".Set(i, ";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << entryType << "::ToInterop(";

			preCallActions << name << "[i]";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << ")";

			preCallActions << ");\n";
			break;
		case ::TypeCategory::MonoObject:
			preCallActions << "\t\t\t\t" << arrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ::TypeCategory::Class:
		case ::TypeCategory::ReflectableClass:
		{
			std::string elemName = "arrayElem" + name;
			preCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
			preCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName,
				entryType, name + "[i]", false, "\t\t\t");
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << elemName << ");" << std::endl;
		}
		break;
		default: // Some resource or game object type
		{
			std::string scriptName = "script" + name;

			preCallActions << generateNativeToScriptObjectLine(paramTypeInfo.TypeCategory, varTypeInfo.flags, scriptName, name + "[i]", "\t\t\t");
			preCallActions << "\t\t\tif(" << scriptName << "[i] != nullptr)\n";
			preCallActions << "\t\t\t" << arrayName << ".Set(i, " << scriptName << "->GetManagedInstance());" << std::endl;
			preCallActions << "\t\t\telse\n";
			preCallActions << "\t\t\t\t" << arrayName << ".Set(i, nullptr);" << std::endl;
		}
		break;
		}

		preCallActions << "\t\t}" << std::endl;
		preCallActions << "\t\t" << argName << " = " << arrayName << ".GetInternal();" << std::endl;

		return argName;
	}
}

std::string generateCppMethodBody(const ClassInfo& classInfo, const MethodInfo& methodInfo, const std::string& sourceClassName,
	const std::string& interopClassName, ::TypeCategory classType, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;

	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;
	bool isExternal = (methodInfo.flags & (int)MethodFlags::External) != 0;

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeInfo;
	if (!methodInfo.returnInfo.typeName.empty() && !isCtor)
	{
		returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags))
			returnAsParameter = true;
		else
		{
			std::string returnType = getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags);
			postCallActions << "\t\t" << returnType << " __output;" << std::endl;

			std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

			returnAssignment = argName + " = ";
			returnStmt = "\t\treturn __output;";
		}
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		bool isLast = (I + 1) == methodInfo.paramInfos.end();

		std::string argName = generateMethodBodyBlockForParam(I->name, *I, isLast, false, preCallActions, postCallActions);

		if (!isArrayOrVector(I->flags))
		{
			TypeMappingInformation paramTypeInfo = getTypeInfo(I->typeName, I->flags);

			methodArgs << getAsManagedToCppArgument(argName, paramTypeInfo.TypeCategory, I->flags, methodInfo.sourceName);
		}
		else
			methodArgs << getAsManagedToCppArgumentPlain(argName, I->flags, isOutput(I->flags), methodInfo.sourceName);

		if (!isLast)
			methodArgs << ", ";
	}

	if (returnAsParameter)
	{
		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	if (isCtor)
	{
		bool isValid = false;
		if (!isExternal)
		{
			if (isClassType(classType))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = bs_shared_ptr_new<" << sourceClassName << ">(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;

			if (isClassType(classType))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
			else if (classType == ::TypeCategory::Resource)
			{
				output << "\t\tResourceHandle<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tScriptResourceManager::Instance().CreateBuiltinScriptResource(instance, managedInstance);" << std::endl;
				isValid = true;
			}
			else if (classType == ::TypeCategory::GUIElement)
			{
				output << "\t\t" << sourceClassName << "* instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}

		if (!isValid)
			outs() << "Error: Cannot generate a constructor for \"" << sourceClassName << "\". Unsupported class type. \n";
	}
	else
	{
		std::stringstream methodCall;
		if (!isExternal)
		{
			if (isStatic)
				methodCall << sourceClassName << "::" << methodInfo.sourceName << "(" << methodArgs.str() << ")"; 
			else if(isModule)
				methodCall << sourceClassName << "::Instance()." << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			else
			{
				methodCall << generateGetInternalLine(sourceClassName, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
				methodCall << "->" << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;
			if (isStatic)
				methodCall << fullMethodName << "(" << methodArgs.str() << ")";
			else
			{
				methodCall << fullMethodName << "(" << generateGetInternalLine(sourceClassName, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);

				std::string methodArgsStr = methodArgs.str();
				if (!methodArgsStr.empty())
					methodCall << ", " << methodArgsStr;

				methodCall << ")";
			}
		}

		std::string call;
		if (!methodInfo.returnInfo.typeName.empty())
		{
			// Dereference input if needed
			if (isClassType(returnTypeInfo.TypeCategory) && !isArrayOrVector(methodInfo.returnInfo.flags))
			{
				if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
					isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
					returnAssignment = "*" + returnAssignment;
			}

			call = getAsCppToInteropArgument(methodCall.str(), returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags, "return");
		}
		else
			call = methodCall.str();

		output << "\t\t" << returnAssignment << call << ";\n";
	}

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	if (!returnStmt.empty())
	{
		output << std::endl;
		output << returnStmt << std::endl;
	}

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppFieldGetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo,	
	::TypeCategory classType, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
	if (!canBeReturned(returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags))
		returnAsParameter = true;
	else
	{
		std::string returnType = getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags);
		postCallActions << "\t\t" << returnType << " __output;" << std::endl;

		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
		returnStmt = "\t\treturn __output;";
	}

	if (returnAsParameter)
	{
		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::Instance()." << fieldInfo.name;
	else
	{
		fieldAccess << generateGetInternalLine(classInfo.name, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
		fieldAccess << "->" << fieldInfo.name;
	}

	// Dereference input if needed
	if (isClassType(returnTypeInfo.TypeCategory) && !isArrayOrVector(methodInfo.returnInfo.flags))
	{
		if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
			isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
			returnAssignment = "*" + returnAssignment;
	}

	std::string access = getAsCppToInteropArgument(fieldAccess.str(), returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags, "return");

	output << "\t\t" << returnAssignment << access << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << std::endl;
	output << returnStmt << std::endl;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppFieldSetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo,
	::TypeCategory classType, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream argValue;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	const VarInfo& paramInfo = methodInfo.paramInfos[0];
	std::string argName = generateMethodBodyBlockForParam(paramInfo.name, paramInfo, false, false, preCallActions, postCallActions);

	TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

	if(!isArrayOrVector(paramInfo.flags))
		argValue << getAsManagedToCppArgument(argName, paramTypeInfo.TypeCategory, paramInfo.flags, methodInfo.sourceName);
	else
		argValue << argName;

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::Instance()." << fieldInfo.name;
	else
	{
		fieldAccess << generateGetInternalLine(classInfo.name, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
		fieldAccess << "->" << fieldInfo.name;
	}

	output << "\t\t" << fieldAccess.str() << " = " << argValue.str() << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppEventCallbackBody(const MethodInfo& eventInfo, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream methodArgs;

	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	int idx = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		bool isLast = (I + 1) == eventInfo.paramInfos.end();

		std::string argName = generateEventCallbackBodyBlockForParam(I->name, *I, preCallActions);

		if (!isArrayOrVector(I->flags))
		{
			TypeMappingInformation paramTypeInfo = getTypeInfo(I->typeName, I->flags);

			if(paramTypeInfo.TypeCategory == ::TypeCategory::Struct)
				methodArgs << getAsCppToManagedArgument(argName, ::TypeCategory::Class, I->flags, eventInfo.sourceName);
			else
				methodArgs << getAsCppToManagedArgument(argName, paramTypeInfo.TypeCategory, I->flags, eventInfo.sourceName);
		}
		else
			methodArgs << getAsCppToManagedArgument(argName, ::TypeCategory::Class, I->flags, eventInfo.sourceName);

		if (!isLast)
			methodArgs << ", ";

		idx++;
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	output << "\t\tMonoUtil::InvokeThunk(" << eventInfo.sourceName << "Thunk";

	if (!isStatic && !isModule)
		output << ", GetManagedInstance()";
	
	if (!eventInfo.paramInfos.empty())
		output << ", " << methodArgs.str();

	output << ");\n";

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppHeaderOutput(const ClassInfo& classInfo, const TypeMappingInformation& typeInfo)
{
	bool inEditor = hasAPIBED (classInfo.api);
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;
	bool isRootBase = classInfo.baseClass.empty();

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	if (!hasStaticEvents)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			if (isStatic)
			{
				hasStaticEvents = true;
				break;
			}
		}
	}

	std::string exportAttr;
	if (!inEditor)
		exportAttr = sFrameworkExportMacro;
	else
		exportAttr = sEditorExportMacro;

	std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.TypeCategory);
	std::string interopBaseClassName;

	std::stringstream output;
	output << generateCppApiCheckBegin(classInfo.api);

	// Generate a common base class if required
	// (GUIElements already have one by default)
	if(typeInfo.TypeCategory != ::TypeCategory::GUIElement)
	{
		if (isBase)
		{
			interopBaseClassName = getScriptInteropType(classInfo.name) + "Base";

			output << "\tclass " << exportAttr << " ";
			output << interopBaseClassName << " : public ";

			if (isRootBase)
			{
				if (typeInfo.TypeCategory == ::TypeCategory::Class)
					output << "ScriptObjectBase";
				if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
					output << "ScriptReflectableBase";
				else if (typeInfo.TypeCategory == ::TypeCategory::Component)
					output << "ScriptComponentBase";
				else if (typeInfo.TypeCategory == ::TypeCategory::Resource)
					output << "ScriptResourceBase";
			}
			else
			{
				std::string parentBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
				output << parentBaseClassName;
			}

			output << std::endl;
			output << "\t{" << std::endl;
			output << "\tpublic:" << std::endl;
			output << "\t\t" << interopBaseClassName << "(MonoObject* instance);" << std::endl;
			output << "\t\tvirtual ~" << interopBaseClassName << "() {}" << std::endl;

			if(!isModule)
			{
				if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " GetInternal() const;\n";
				}
				else if (typeInfo.TypeCategory == ::TypeCategory::Class)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " GetInternal() const { return mInternal; }" << std::endl;

					// Data member only present in the top-most base class
					if (isRootBase)
					{
						output << "\tprotected:" << std::endl;
						output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
					}
				}
			}

			output << "\t};" << std::endl;
			output << std::endl;
		}
		else if (!classInfo.baseClass.empty())
		{
			interopBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
		}
	}

	// Generate main class
	output << "\tclass " << exportAttr << " ";;

	std::string interopClassName = getScriptInteropType(classInfo.name);
	output << interopClassName << " : public ";

	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
		output << "TScriptResource<" << interopClassName << ", " << classInfo.name;
	else if (typeInfo.TypeCategory == ::TypeCategory::Component)
		output << "TScriptComponent<" << interopClassName << ", " << classInfo.name;
	else if (typeInfo.TypeCategory == ::TypeCategory::GUIElement)
		output << "TScriptGUIElement<" << interopClassName;
	else if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
		output << "TScriptReflectable<" << interopClassName << ", " << classInfo.name;
	else // Class
		output << "ScriptObject<" << interopClassName;

	if (!interopBaseClassName.empty())
		output << ", " << interopBaseClassName;

	output << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;

	output << std::endl;

	// Constructor
	if (!isModule)
	{
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeInfo.TypeCategory != ::TypeCategory::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ");\n";
	}
	else
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;

	output << std::endl;

	if (typeInfo.TypeCategory == ::TypeCategory::Class && !isModule)
	{
		// getInternal() method (handle types have getHandle() implemented by their base type)
		if (isBase || !classInfo.baseClass.empty())
			output << "\t\t" << wrappedDataType << " GetInternal() const;\n";
		else
			output << "\t\t" << wrappedDataType << " GetInternal() const { return mInternal; }" << std::endl;
	}

	if(isClassType(typeInfo.TypeCategory) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
			output << "\t\tMonoObject* GetManagedInstance() const;\n";

		// create() method
		output << "\t\tstatic MonoObject* Create(const " << wrappedDataType << "& value);" << std::endl;
		output << std::endl;
	}

	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		// createInstance() method required by script resource manager
		output << "\t\tstatic MonoObject* CreateInstance();" << std::endl;
		output << std::endl;
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\t\tstatic void StartUp();" << std::endl;
		output << "\t\tstatic void ShutDown();" << std::endl;
		output << std::endl;
	}

	output << "\tprivate:" << std::endl;

	// Handle (if required)
	if (isClassType(typeInfo.TypeCategory))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tuint32_t mGCHandle = 0;\n\n";
	}

	// Event callback methods
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t\t" << generateCppEventCallbackSignature(eventInfo, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Data member
	if (typeInfo.TypeCategory == ::TypeCategory::Class && !isModule && classInfo.baseClass.empty() && !isBase)
	{
		output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
		output << std::endl;
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << generateCppEventThunk(eventInfo, isModule);
		output << generateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Event handles
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << generateCppApiCheckBegin(eventInfo.api);
			output << "\t\tstatic HEvent " << eventInfo.sourceName << "Conn;" << std::endl;
			output << generateApiCheckEnd(eventInfo.api);
		}
	}

	if(hasStaticEvents)
		output << std::endl;

	// CLR hooks
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeInfo.TypeCategory == ::TypeCategory::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
		output << "\t\tstatic MonoObject* InternalGetRef(" << interopClassThisPtrType << "* thisPtr);\n\n";

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	output << "\t};" << std::endl;
	output << generateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppSourceOutput(const ClassInfo& classInfo, const TypeMappingInformation& typeInfo)
{
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	for(auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		if(isStatic)
		{
			hasStaticEvents = true;
			break;
		}
	}

	std::string interopClassName = getScriptInteropType(classInfo.name);
	std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.TypeCategory);

	std::string interopBaseClassName;

	if(typeInfo.TypeCategory != ::TypeCategory::GUIElement)
	{
		if (isBase)
			interopBaseClassName = getScriptInteropType(classInfo.name) + "Base";
		else if (!classInfo.baseClass.empty())
			interopBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
	}

	std::stringstream output;
	output << generateCppApiCheckBegin(classInfo.api);

	if (isBase && typeInfo.TypeCategory != ::TypeCategory::GUIElement)
	{
		// Base class constructor
		output << "\t" << interopBaseClassName << "::" << interopBaseClassName << "(MonoObject* managedInstance)\n";
		output << "\t\t:";

		bool isRootBase = classInfo.baseClass.empty();
		if (isRootBase)
		{
			if (typeInfo.TypeCategory == ::TypeCategory::Class)
				output << "ScriptObjectBase";
			if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
				output << "ScriptReflectableBase";
			else if (typeInfo.TypeCategory == ::TypeCategory::Component)
				output << "ScriptComponentBase";
			else if (typeInfo.TypeCategory == ::TypeCategory::Resource)
				output << "ScriptResourceBase";
		}
		else
		{
			std::string parentBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
			output << parentBaseClassName;
		}

		output << "(managedInstance)\n";
		output << "\t { }\n";
		output << "\n";

		// Base class getInternal() method
		if(typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
		{
			output << "\t" << wrappedDataType << " " << interopBaseClassName << "::" << "GetInternal() const\n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n";
		}
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t" << interopClassName << "::" << eventInfo.sourceName << "ThunkDef " << interopClassName << "::" << eventInfo.sourceName << "Thunk; \n";
		output << generateApiCheckEnd(eventInfo.api);
	}

	if (!classInfo.eventInfos.empty())
		output << "\n";

	// Event handles
	bool hasEventHandles = false;
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << generateCppApiCheckBegin(eventInfo.api);
			output << "\tHEvent " << interopClassName << "::" << eventInfo.sourceName << "Conn;\n";
			output << generateApiCheckEnd(eventInfo.api);

			hasEventHandles = true;
		}
	}

	if (hasEventHandles)
		output << "\n";

	// Constructor
	if (!isModule)
	{
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeInfo.TypeCategory != ::TypeCategory::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ")\n";
	}
	else
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;

	output << "\t\t:";

	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
		output << "TScriptResource(managedInstance, value)";
	else if (typeInfo.TypeCategory == ::TypeCategory::Component)
		output << "TScriptComponent(managedInstance, value)";
	else if (typeInfo.TypeCategory == ::TypeCategory::GUIElement)
		output << "TScriptGUIElement(managedInstance, value)";
	else if (typeInfo.TypeCategory == ::TypeCategory::ReflectableClass)
	{
		if(!isModule)
			output << "TScriptReflectable(managedInstance, value)";
		else
			output << "TScriptReflectable(managedInstance, nullptr)";
	}
	else // Class
	{
		if(!isModule && !isBase && classInfo.baseClass.empty())
			output << "ScriptObject(managedInstance), mInternal(value)";
		else
			output << "ScriptObject(managedInstance)";
	}
	output << std::endl;
	output << "\t{" << std::endl;

	if (isClassType(typeInfo.TypeCategory))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tmGCHandle = MonoUtil::NewWeakGcHandle(managedInstance);\n";

		if (!isModule && (isBase || !classInfo.baseClass.empty()))
			output << "\t\tmInternal = value;\n";
	}

	// Register any non-static events
	if (!isModule)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isStatic)
			{
				output << generateCppApiCheckBegin(eventInfo.api);

				if (!isCallback)
					output << "\t\tvalue->" << eventInfo.sourceName << ".Connect(";
				else
					output << "\t\tvalue->" << eventInfo.sourceName << " = ";

				output << "std::bind(&" << interopClassName << "::" << eventInfo.interopName << ", this";

				for (int i = 0; i < (int)eventInfo.paramInfos.size(); i++)
					output << ", std::placeholders::_" << (i + 1);

				if (!isCallback)
					output << ")";

				output << ");\n";
				output << generateApiCheckEnd(eventInfo.api);
			}
		}
	}

	output << "\t}" << std::endl;
	output << std::endl;

	if (typeInfo.TypeCategory == ::TypeCategory::Class)
	{
		// getInternal method
		if (isBase || !classInfo.baseClass.empty())
		{
			output << "\t" << wrappedDataType << " " << interopClassName << "::GetInternal() const \n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n\n";
		}
	}

	if (isClassType(typeInfo.TypeCategory) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
		{
			output << "\tMonoObject* " << interopClassName << "::GetManagedInstance() const\n";
			output << "\t{\n";
			output << "\t\treturn MonoUtil::GetObjectFromGCHandle(mGCHandle);\n";
			output << "\t}\n\n";
		}
	}

	// CLR hook registration
	output << "\tvoid " << interopClassName << "::InitRuntimeData()" << std::endl;
	output << "\t{" << std::endl;

	// Internal_GetRef interop method
	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_GetRef\", (void*)&" <<
			interopClassName << "::InternalGetRef);\n";
	}

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal" << methodInfo.interopName << ");" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal" << methodInfo.interopName << ");" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	output << std::endl;

	for(auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t\t" << eventInfo.sourceName << "Thunk = ";
		output << "(" << eventInfo.sourceName << "ThunkDef)metaData.ScriptClass->GetMethodExact(";
		output << "\"Internal_" << eventInfo.interopName << "\", \"";

		for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
		{
			const VarInfo& paramInfo = *I;
			TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

			std::string typeName;

			// Generic types require `X after their name
			StringRef inputStr(paramTypeInfo.ScriptTypeName.data(), paramTypeInfo.ScriptTypeName.length());
			inputStr = inputStr.trim();

			const size_t leftBracketIdx = inputStr.find_first_of('<');
			const size_t rightBracketIdx = inputStr.find_last_of('>');
			const size_t numLeftBrackets = inputStr.count('<');
			const size_t numRightBrackets = inputStr.count('>');

			if (numLeftBrackets > 1 || numRightBrackets > 1)
			{
				outs() << "Error: Cannot parse event signature type. Nested generic parameters are not allowed.\n";
				typeName = paramTypeInfo.ScriptTypeName;
			}
			else if (leftBracketIdx != StringRef::npos && rightBracketIdx != StringRef::npos)
			{
				StringRef templateType = inputStr.substr(0, leftBracketIdx);
				StringRef templateArgs = inputStr.substr(leftBracketIdx + 1, rightBracketIdx - leftBracketIdx - 1);
				const size_t numTemplateArgs = templateArgs.count(',') + 1;

				typeName = templateType.str() + "`" + std::to_string(numTemplateArgs) + "<" + templateArgs.str() + ">";
			}
			else
				typeName = paramTypeInfo.ScriptTypeName;

			if(typeName == "float")
				typeName = "single";

			std::string csType = getCSVarType(typeName, paramTypeInfo.TypeCategory, paramInfo.flags, true, true, true, true);

			output << csType;

			if ((I + 1) != eventInfo.paramInfos.end())
				output << ",";
		}

		output << "\")->GetThunk();" << std::endl;
		output << generateApiCheckEnd(eventInfo.api);
	}

	output << "\t}" << std::endl;
	output << std::endl;

	// create() or createInstance() methods
	if ((isClassType(typeInfo.TypeCategory) && !isModule) || typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		std::stringstream ctorSignature;
		std::stringstream ctorParamsInit;
		MethodInfo unusedCtor = findUnusedCtorSignature(classInfo);
		int numDummyParams = (int)unusedCtor.paramInfos.size();

		ctorParamsInit << "\t\tbool dummy = false;" << std::endl;
		ctorParamsInit << "\t\tvoid* ctorParams[" << numDummyParams << "] = { ";

		for (int i = 0; i < numDummyParams; i++)
		{
			ctorParamsInit << "&dummy";
			ctorSignature << unusedCtor.paramInfos[i].typeName;

			if ((i + 1) < numDummyParams)
			{
				ctorParamsInit << ", ";
				ctorSignature << ",";
			}
		}

		ctorParamsInit << " };" << std::endl;
		ctorParamsInit << std::endl;

		if (isClassType(typeInfo.TypeCategory))
		{
			output << "\tMonoObject* " << interopClassName << "::Create(const " << wrappedDataType << "& value)" << std::endl;
			output << "\t{" << std::endl;
			output << "\t\tif(value == nullptr) return nullptr; " << std::endl;
			output << std::endl;

			output << ctorParamsInit.str();
			output << "\t\tMonoObject* managedInstance = metaData.ScriptClass->CreateInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;
			output << "\t\tnew (bs_alloc<" << interopClassName << ">()) " << interopClassName << "(managedInstance, value);" << std::endl;
			output << "\t\treturn managedInstance;" << std::endl;

			output << "\t}" << std::endl;
		}
		else if (typeInfo.TypeCategory == ::TypeCategory::Resource)
		{
			output << "\t MonoObject*" << interopClassName << "::CreateInstance()" << std::endl;
			output << "\t{" << std::endl;

			output << ctorParamsInit.str();
			output << "\t\treturn metaData.ScriptClass->CreateInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;

			output << "\t}" << std::endl;
		}
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\tvoid " << interopClassName << "::StartUp()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isCallback)
			{
				if (isStatic)
				{
					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::" << eventInfo.sourceName << ".Connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
				else if (isModule)
				{
					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::Instance()." << eventInfo.sourceName << ".Connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
			}
			else
			{
				if (isStatic)
					output << classInfo.name << "::" << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
				else if (isModule)
					output << classInfo.name << "::Instance()." << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
			}
		}

		output << "\t}" << std::endl;

		output << "\tvoid " << interopClassName << "::ShutDown()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if(!isCallback && (isStatic || isModule))
				output << "\t\t" << eventInfo.sourceName << "Conn.Disconnect();" << std::endl;
		}

		output << "\t}" << std::endl;
		output << std::endl;
	}

	// Event callback method implementations
	for (auto I = classInfo.eventInfos.begin(); I != classInfo.eventInfos.end(); ++I)
	{
		const MethodInfo& eventInfo = *I;

		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t" << generateCppEventCallbackSignature(eventInfo, interopClassName, isModule) << std::endl;
		output << generateCppEventCallbackBody(eventInfo, isModule);
		output << generateApiCheckEnd(eventInfo.api);

		if ((I + 1) != classInfo.eventInfos.end())
			output << std::endl;
	}

	// CLR hook method implementations
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeInfo.TypeCategory == ::TypeCategory::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		output << "\tMonoObject* " << interopClassName << "::InternalGetRef(" << interopClassThisPtrType << "* thisPtr)\n";
		output << "\t{\n";
		output << "\t\treturn thisPtr->GetRRef();\n";
		output << "\t}\n\n";
	}

	// Constructors
	for (auto I = classInfo.ctorInfos.begin(); I != classInfo.ctorInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t" << generateCppMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeInfo.TypeCategory, isModule);
		output << generateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Methods
	for (auto I = classInfo.methodInfos.begin(); I != classInfo.methodInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		if ((methodInfo.flags & (int)MethodFlags::FieldWrapper) != 0)
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t" << generateCppMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeInfo.TypeCategory, isModule);
		output << generateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Field wrapper methods
	for(auto I = classInfo.fieldInfos.begin(); I != classInfo.fieldInfos.end(); ++I)
	{
		const MethodInfo* setterInfo = nullptr;
		const MethodInfo* getterInfo = nullptr;

		std::string getterName = "Get" + I->name;
		std::string setterName = "Set" + I->name;
		for(auto& entry : classInfo.methodInfos)
		{
			if ((entry.flags & (int)MethodFlags::FieldWrapper) == 0)
				continue;

			if (entry.sourceName == getterName)
				getterInfo = &entry;
			else if (entry.sourceName == setterName)
				setterInfo = &entry;

			if (getterInfo != nullptr && setterInfo != nullptr)
				break;
		}

		assert(getterInfo && setterInfo);

		output << generateCppApiCheckBegin(getterInfo->api);
		output << "\t" << generateCppMethodSignature(*getterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppFieldGetterBody(classInfo, *I, *getterInfo, typeInfo.TypeCategory, isModule);
		output << generateApiCheckEnd(getterInfo->api);
		
		output << std::endl;

		output << generateCppApiCheckBegin(setterInfo->api);
		output << "\t" << generateCppMethodSignature(*setterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppFieldSetterBody(classInfo, *I, *setterInfo, typeInfo.TypeCategory, isModule);
		output << generateApiCheckEnd(setterInfo->api);
			
		if ((I + 1) != classInfo.fieldInfos.end())
			output << std::endl;
	}

	output << generateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppStructHeader(const StructInfo& structInfo)
{
	TypeMappingInformation typeInfo = getTypeInfo(structInfo.name, 0);

	std::stringstream output;
	output << generateCppApiCheckBegin(structInfo.api);

	if(structInfo.requiresInterop)
	{
		output << "\tstruct " << structInfo.interopName << "\n";
		output << "\t{\n";

		for(auto& fieldInfo : structInfo.fields)
		{
			TypeMappingInformation fieldTypeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

			output << "\t\t";
			output << getInteropCppVarType(fieldInfo.typeName, fieldTypeInfo.TypeCategory, fieldInfo.flags, true);
			output << " " << fieldInfo.name << ";\n";
		}

		output << "\t};\n\n";
	}

	output << "\tclass ";

	bool inEditor = hasAPIBED (structInfo.api);
	if (!inEditor)
		output << sFrameworkExportMacro << " ";
	else
		output << sEditorExportMacro << " ";

	std::string interopClassName = getScriptInteropType(structInfo.name);
	output << interopClassName << " : public " << "ScriptObject<" << interopClassName << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeInfo.ScriptTypeName << "\")" << std::endl;

	output << std::endl;

	output << "\t\tstatic MonoObject* Box(const " << structInfo.interopName << "& value);" << std::endl;
	output << "\t\tstatic " << structInfo.interopName << " Unbox(MonoObject* value);" << std::endl;

	if(structInfo.requiresInterop)
	{
		output << "\t\tstatic " << structInfo.name << " FromInterop(const " << structInfo.interopName << "& value);\n";
		output << "\t\tstatic " << structInfo.interopName << " ToInterop(const " << structInfo.name << "& value);\n";
	}

	output << std::endl;
	output << "\tprivate:" << std::endl;

	// Constructor
	output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;
	output << std::endl;

	output << "\t};" << std::endl;
	output << generateApiCheckEnd(structInfo.api);

	return output.str();
}

std::string generateCppStructSource(const StructInfo& structInfo)
{
	TypeMappingInformation typeInfo = getTypeInfo(structInfo.name, 0);
	std::string interopClassName = getScriptInteropType(structInfo.name);

	std::stringstream output;
	output << generateCppApiCheckBegin(structInfo.api);

	// Constructor
	output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;
	output << "\t\t:ScriptObject(managedInstance)" << std::endl;
	output << "\t{ }" << std::endl;
	output << std::endl;

	// Empty initRuntimeData
	output << "\tvoid " << interopClassName << "::InitRuntimeData()" << std::endl;
	output << "\t{ }" << std::endl;
	output << std::endl;

	// Box
	output << "\tMonoObject*" << interopClassName << "::Box(const " << structInfo.interopName << "& value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn MonoUtil::Box(metaData.ScriptClass->GetInternalClassInternal(), (void*)&value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	// Unbox
	output << "\t" << structInfo.interopName << " " << interopClassName << "::Unbox(MonoObject* value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn *(" << structInfo.interopName << "*)MonoUtil::Unbox(value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	if(structInfo.requiresInterop)
	{
		// Convert from interop
		output << "\t" << structInfo.name << " " << interopClassName << "::FromInterop(const " << structInfo.interopName << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.name << " output;\n";
		for (auto& fieldInfo : structInfo.fields)
		{
			// Arrays can be assigned, so copy them entry by entry
			if(isArray(fieldInfo.flags))
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, false, output);

				output << "\t\tauto tmp" << fieldInfo.name << " = " << argName << ";\n";
				output << "\t\tfor(int i = 0; i < " << fieldInfo.arraySize << "; ++i)\n";
				output << "\t\t\toutput." << fieldInfo.name << "[i] = tmp" << fieldInfo.name << "[i];\n";
			}
			else
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, false, output);

				output << "\t\toutput." << fieldInfo.name << " = " << argName << ";\n";
			}
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";

		// Convert to interop
		output << "\t" << structInfo.interopName << " " << interopClassName << "::ToInterop(const " << structInfo.name << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.interopName << " output;\n";
		for(auto& fieldInfo : structInfo.fields)
		{
			std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, true, output);

			output << "\t\toutput." << fieldInfo.name << " = " << argName << ";\n";
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";
	}

	output << generateApiCheckEnd(structInfo.api);
	return output.str();
}

std::string generateCSStyleAttributes(const ExportStyle& style, const TypeMappingInformation& typeInfo, int typeFlags, bool isStruct)
{
	std::stringstream output;
	
	if(((style.flags & (int)StyleFlags::AsLayerMask) != 0) && isInt64(typeInfo))
		output << "\t\t[LayerMask]\n";

	if ((style.flags & (int)StyleFlags::Step) != 0)
		output << "\t\t[Step(" << style.step << "f)]\n";

	if ((style.flags & (int)StyleFlags::Range) != 0)
	{
		std::string isSlider = ((style.flags & (int)StyleFlags::AsSlider) != 0) ? "true" : "false";
		output << "\t\t[Range(" << style.rangeMin << "f, " << style.rangeMax << "f, " << isSlider << ")]\n";
	}
	else if ((style.flags & (int)StyleFlags::AsSlider) != 0)
		output << "\t\t[Range(float.MinValue, float.MaxValue, true)]\n";

	if(((style.flags & (int)StyleFlags::Order) != 0))
		output << "\t\t[Order(" << style.order << ")]\n";

	if(((style.flags & (int)StyleFlags::Category) != 0))
		output << "\t\t[Category(\"" << style.category << "\")]\n";

	if(((style.flags & (int)StyleFlags::Inline) != 0))
		output << "\t\t[Inline]\n";

	bool notNull = (style.flags & (int)StyleFlags::NotNull) != 0;
	bool passByCopy = (style.flags & (int)StyleFlags::PassByCopy) != 0;

	if(!isStruct && (isClassType(typeInfo.TypeCategory) && isPassedByValue(typeFlags)))
	{
		notNull = true;
		passByCopy = true;
	}

	if(notNull)
		output << "\t\t[NotNull]\n";

	if(passByCopy)
		output << "\t\t[PassByCopy]\n";

	if(((style.flags & (int)StyleFlags::ApplyOnDirty) != 0))
		output << "\t\t[ApplyOnDirty]\n";

	if(((style.flags & (int)StyleFlags::AsQuaternion) != 0))
		output << "\t\t[AsQuaternion]\n";

	if(((style.flags & (int)StyleFlags::LoadOnAssign) != 0))
		output << "\t\t[LoadOnAssign]\n";

	if(((style.flags & (int)StyleFlags::HDR) != 0))
		output << "\t\t[HDR]\n";

	return output.str();
}

std::string generateCSDefaultValueAssignment(const VarInfo& paramInfo)
{
	if (paramInfo.defaultValueType.empty() || isFlagsEnum(paramInfo.flags))
		return paramInfo.defaultValue;
	else
	{
		// Constructor or cast, assuming constructor as cast implies a constructor accepting the type exists (and we don't export cast operators anyway)
		TypeMappingInformation defaultValTypeInfo = getTypeInfo(paramInfo.defaultValueType, 0);

		if(defaultValTypeInfo.TypeCategory == ::TypeCategory::Struct && paramInfo.defaultValue.empty())
			return defaultValTypeInfo.ScriptTypeName + ".Default()";
		else
			return "new " + defaultValTypeInfo.ScriptTypeName + "(" + paramInfo.defaultValue + ")";
	}
}

std::string generateCSMethodParams(const MethodInfo& methodInfo, bool forInterop)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;

		if(!forInterop && !paramInfo.defaultValueType.empty() && !isFlagsEnum(paramInfo.flags))
		{
			// We don't generate parameters that have complex default values (as they're not supported in C#).
			// Instead the post-processor has generated different versions of this method, so we can just skip
			// such parameters
			continue;
		}

		if (I != methodInfo.paramInfos.begin())
			output << ", ";

		TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
		std::string qualifiedType = getCSVarType(paramTypeInfo.ScriptTypeName, paramTypeInfo.TypeCategory, paramInfo.flags, true, true, forInterop);

		bool isLastParam = (I + 1) == methodInfo.paramInfos.end();
		if (isVarParam(paramInfo.flags) && isLastParam)
			output << "params ";

		output << qualifiedType << " " << paramInfo.name;

		if (!forInterop && !paramInfo.defaultValue.empty())
			output << " = " << generateCSDefaultValueAssignment(paramInfo);
	}

	return output.str();
}

std::string generateCSMethodArgs(const MethodInfo& methodInfo, bool forInterop)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

		if (isOutput(paramInfo.flags))
			output << "out ";
		else if (forInterop && isPlainStruct(paramTypeInfo.TypeCategory, paramInfo.flags))
			output << "ref ";

		output << paramInfo.name;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSMethodDefaultParamAssignments(const MethodInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		
		if (paramInfo.defaultValueType.empty() || isFlagsEnum(paramInfo.flags))
			continue;

		if (paramInfo.defaultValueType == "null" || paramInfo.defaultValue == "null")
		{
			TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
			output << indent << paramTypeInfo.ScriptTypeName << " " << paramInfo.name << " = " << paramInfo.defaultValue << ";\n";
		}
		else
		{
			TypeMappingInformation defaultValTypeInfo = getTypeInfo(paramInfo.defaultValueType, 0);
			output << indent << defaultValTypeInfo.ScriptTypeName << " " << paramInfo.name << " = ";
			output << "new " << defaultValTypeInfo.ScriptTypeName << "(" << paramInfo.defaultValue << ");\n";
		}
	}

	return output.str();
	
}

std::string generateCSEventSignature(const MethodInfo& methodInfo)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		TypeMappingInformation paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
		std::string type = getCSVarType(paramTypeInfo.ScriptTypeName, paramTypeInfo.TypeCategory, paramInfo.flags, false, true, false);

		output << type;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSEventArgs(const MethodInfo& methodInfo)
{
	std::stringstream output;

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		output << I->name;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSInteropMethodSignature(const MethodInfo& methodInfo, const std::string& csClassName, bool isModule)
{
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.returnInfo.typeName.empty() || isCtor)
		output << "void";
	else
	{
		TypeMappingInformation returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			std::string qualifiedType = getCSVarType(returnTypeInfo.ScriptTypeName, returnTypeInfo.TypeCategory,
				methodInfo.returnInfo.flags, false, true, false);
			output << qualifiedType;
		}
	}

	output << " ";

	output << "Internal_" << methodInfo.interopName << "(";

	if (isCtor)
	{
		output << csClassName << " managedInstance";

		if (methodInfo.paramInfos.size() > 0)
			output << ", ";
	}
	else if (!isStatic && !isModule)
	{
		output << "IntPtr thisPtr";

		if (methodInfo.paramInfos.size() > 0 || returnAsParameter)
			output << ", ";
	}

	output << generateCSMethodParams(methodInfo, true);

	if (returnAsParameter)
	{
		TypeMappingInformation returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		std::string qualifiedType = getCSVarType(returnTypeInfo.ScriptTypeName, returnTypeInfo.TypeCategory, methodInfo.returnInfo.flags, false, true, false);

		if (methodInfo.paramInfos.size() > 0)
			output << ", ";

		output << "out " << qualifiedType << " __output";
	}

	output << ")";
	return output.str();
}

std::string generateCSClass(ClassInfo& input, TypeMappingInformation& typeInfo)
{
	bool isModule = (input.flags & (int)ClassFlags::IsModule) != 0;

	std::stringstream ctors;
	std::stringstream properties;
	std::stringstream events;
	std::stringstream methods;
	std::stringstream interops;

	// Private constructor for runtime use
	MethodInfo pvtCtor = findUnusedCtorSignature(input);
	ctors << "\t\tprivate " << typeInfo.ScriptTypeName << "(" << generateCSMethodParams(pvtCtor, false) << ") { }" << std::endl;

	// Parameterless constructor in case anything derives from this class
	if (!hasParameterlessConstructor(input))
		ctors << "\t\tprotected " << typeInfo.ScriptTypeName << "() { }" << std::endl;

	ctors << std::endl;

	// Constructors
	for (auto& entry : input.ctorInfos)
	{
		if (!isCSOnly(entry.flags))
		{
			// Generate interop
			interops << generateCsApiCheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(" << typeInfo.ScriptTypeName << " managedInstance";

			if (entry.paramInfos.size() > 0)
				interops << ", " << generateCSMethodParams(entry, true);

			interops << ");\n";
			interops << generateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		ctors << generateCsApiCheckBegin(entry.api);
		ctors << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

		if (entry.visibility == CSVisibility::Internal)
			ctors << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			ctors << "\t\tprivate ";
		else
			ctors << "\t\tpublic ";

		ctors << typeInfo.ScriptTypeName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
		ctors << "\t\t{" << std::endl;
		ctors << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");
		ctors << "\t\t\tInternal_" << entry.interopName << "(this";

		if (entry.paramInfos.size() > 0)
			ctors << ", " << generateCSMethodArgs(entry, true);

		ctors << ");" << std::endl;
		ctors << "\t\t}" << std::endl;
		ctors << generateApiCheckEnd(entry.api);
		ctors << std::endl;
	}

	// 'Ref' property & conversion operator to RRef<T>
	if(typeInfo.TypeCategory == ::TypeCategory::Resource)
	{
		interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n";
		interops << "\t\tprivate static extern RRef<" << typeInfo.ScriptTypeName << "> Internal_GetRef(IntPtr thisPtr);\n";

		properties << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		properties << "\t\tpublic RRef<" << typeInfo.ScriptTypeName << "> Ref\n";
		properties << "\t\t{\n";
		properties << "\t\t\tget { return Internal_GetRef(mCachedPtr); }\n";
		properties << "\t\t}\n";
		properties << "\n";

		methods << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		methods << "\t\tpublic static implicit operator RRef<" << typeInfo.ScriptTypeName << ">(" << typeInfo.ScriptTypeName << " x)\n";
		methods << "\t\t{\n";
		methods << "\t\t\tif(x != null)\n";
		methods << "\t\t\t\treturn Internal_GetRef(x.mCachedPtr);\n";
		methods << "\t\t\telse\n";
		methods << "\t\t\t\treturn null;\n"; 
		methods << "\t\t}\n\n";
	}

	// External constructors, methods and interop stubs
	for (auto& entry : input.methodInfos)
	{
		// Generate interop
		if (!isCSOnly(entry.flags))
		{
			interops << generateCsApiCheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern " << generateCSInteropMethodSignature(entry, typeInfo.ScriptTypeName, isModule) << ";";
			interops << std::endl;
			interops << generateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;

		if (isConstructor)
		{
			ctors << generateCsApiCheckBegin(entry.api);
			ctors << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

			if (entry.visibility == CSVisibility::Internal)
				ctors << "\t\tinternal ";
			else if (entry.visibility == CSVisibility::Private)
				ctors << "\t\tprivate ";
			else
				ctors << "\t\tpublic ";

			ctors << typeInfo.ScriptTypeName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
			ctors << "\t\t{" << std::endl;
			ctors << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");
			ctors << "\t\t\tInternal_" << entry.interopName << "(this";

			if (entry.paramInfos.size() > 0)
				ctors << ", " << generateCSMethodArgs(entry, true);

			ctors << ");" << std::endl;
			ctors << "\t\t}" << std::endl;
			ctors << generateApiCheckEnd(entry.api);
			ctors << std::endl;
		}
		else
		{
			bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);
			if (!isProperty)
			{
				TypeMappingInformation returnTypeInfo;
				std::string returnType;
				if (entry.returnInfo.typeName.empty())
					returnType = "void";
				else
				{
					returnTypeInfo = getTypeInfo(entry.returnInfo.typeName, entry.returnInfo.flags);
					returnType = getCSVarType(returnTypeInfo.ScriptTypeName, returnTypeInfo.TypeCategory, entry.returnInfo.flags, false, true, false);
				}

				methods << generateCsApiCheckBegin(entry.api);
				methods << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

				if (entry.visibility == CSVisibility::Internal)
					methods << "\t\tinternal ";
				else if (entry.visibility == CSVisibility::Private)
					methods << "\t\tprivate ";
				else
					methods << "\t\tpublic ";

				if (isStatic || isModule)
					methods << "static ";

				methods << returnType << " " << entry.scriptName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
				methods << "\t\t{" << std::endl;
				methods << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");

				bool returnByParam = false;
				if (!entry.returnInfo.typeName.empty())
				{
					if (!canBeReturned(returnTypeInfo.TypeCategory, entry.returnInfo.flags))
					{
						methods << "\t\t\t" << returnType << " temp;" << std::endl;
						methods << "\t\t\tInternal_" << entry.interopName << "(";
						returnByParam = true;
					}
					else
						methods << "\t\t\treturn Internal_" << entry.interopName << "(";
				}
				else
					methods << "\t\t\tInternal_" << entry.interopName << "(";

				if (!isStatic && !isModule)
				{
					methods << "mCachedPtr";

					if (entry.paramInfos.size() > 0 || returnByParam)
						methods << ", ";
				}

				methods << generateCSMethodArgs(entry, true);

				if (returnByParam)
				{
					if (entry.paramInfos.size() > 0)
						methods << ", ";

					methods << "out temp";
				}

				methods << ");" << std::endl;

				if (returnByParam)
					methods << "\t\t\treturn temp;" << std::endl;

				methods << "\t\t}" << std::endl;
				methods << generateApiCheckEnd(entry.api);
				methods << std::endl;
			}
		}
	}

	// Properties
	for (auto& entry : input.propertyInfos)
	{
		TypeMappingInformation propTypeInfo = getTypeInfo(entry.type, entry.typeFlags);
		std::string propTypeName = getCSVarType(propTypeInfo.ScriptTypeName, propTypeInfo.TypeCategory, entry.typeFlags, false, true, false);

		properties << generateCsApiCheckBegin(entry.api);
		properties << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

		bool defaultVisible = entry.visibility != CSVisibility::Internal && entry.visibility != CSVisibility::Private &&
			!entry.setter.empty();
		if (defaultVisible)
		{
			if ((entry.style.flags & (int)StyleFlags::ForceHide) == 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}
		else
		{
			if ((entry.style.flags & (int)StyleFlags::ForceShow) != 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}

		properties << generateCSStyleAttributes(entry.style, propTypeInfo, entry.typeFlags, false);

		properties << "\t\t[NativeWrapper]\n";

		if (entry.visibility == CSVisibility::Internal)
			properties << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			properties << "\t\tprivate ";
		else
			properties << "\t\tpublic ";

		if (entry.isStatic || isModule)
			properties << "static ";

		properties << propTypeName << " " << entry.name << std::endl;
		properties << "\t\t{" << std::endl;

		if (!entry.getter.empty())
		{
			if (canBeReturned(propTypeInfo.TypeCategory, entry.typeFlags))
			{
				properties << "\t\t\tget { return Internal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr";

				properties << "); }" << std::endl;
			}
			else
			{
				properties << "\t\t\tget" << std::endl;
				properties << "\t\t\t{" << std::endl;
				properties << "\t\t\t\t" << propTypeName << " temp;" << std::endl;

				properties << "\t\t\t\tInternal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr, ";

				properties << "out temp);" << std::endl;

				properties << "\t\t\t\treturn temp;" << std::endl;
				properties << "\t\t\t}" << std::endl;
			}
		}

		if (!entry.setter.empty())
		{
			properties << "\t\t\tset { Internal_" << entry.setter << "(";

			if (!entry.isStatic && !isModule)
				properties << "mCachedPtr, ";

			if(isPlainStruct(propTypeInfo.TypeCategory, entry.typeFlags))
				properties << "ref ";

			properties << "value); }" << std::endl;
		}

		properties << "\t\t}" << std::endl;
		properties << generateApiCheckEnd(entry.api);
		properties << std::endl;
	}

	// Events & callbacks
	for(auto& entry : input.eventInfos)
	{
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
		bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

		events << generateCsApiCheckBegin(entry.api);
		events << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");
		events << "\t\t";

		if (!isCallback && !isInternal)
		{
			if (entry.visibility == CSVisibility::Internal)
				events << "internal ";
			else if (entry.visibility == CSVisibility::Private)
				events << "private ";
			else
				events << "public ";
		}

		if (isStatic || isModule)
			events << "static ";

		if (!isCallback && !isInternal)
		{
			events << "event Action";

			if (!entry.paramInfos.empty())
				events << "<" << generateCSEventSignature(entry) << ">";
		
			events << " " << entry.scriptName << ";\n\n";
		}
		else
		{
			events << "partial void Callback_" << entry.scriptName << "(";

			if (!entry.paramInfos.empty())
				events << generateCSMethodParams(entry, false);
		
			events << ");\n";
			events << generateApiCheckEnd(entry.api);
			events << "\n";
		}		

		// Event interop
		interops << generateCsApiCheckBegin(entry.api);

		interops << "\t\tprivate ";

		if (isStatic || isModule)
			interops << "static ";

		interops << "void Internal_" << entry.interopName << "(" << generateCSMethodParams(entry, true) << ")" << std::endl;
		interops << "\t\t{" << std::endl;
		if (!isCallback && !isInternal)
			interops << "\t\t\t" << entry.scriptName << "?.Invoke(" << generateCSEventArgs(entry) << ");\n";
		else
			interops << "\t\t\tCallback_" << entry.scriptName << "(" << generateCSEventArgs(entry) << ");\n";
		interops << "\t\t}" << std::endl;
		interops << generateApiCheckEnd(entry.api);
	}

	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(input.documentation, "\t");

	// Force non-resource and non-component types to show in inspector, except explicitly hidden
	if (isClassType(typeInfo.TypeCategory) || (input.flags & (int)ClassFlags::HideInInspector) == 0)
		output << "\t[ShowInInspector]\n";

	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string baseType;
	if (!input.baseClass.empty())
	{
		TypeMappingInformation baseTypeInfo = getTypeInfo(input.baseClass, 0);
		baseType = baseTypeInfo.ScriptTypeName;
	}
	else if (typeInfo.TypeCategory == ::TypeCategory::Resource)
		baseType = "Resource";
	else if (typeInfo.TypeCategory == ::TypeCategory::Component)
		baseType = "Component";
	else if (typeInfo.TypeCategory == ::TypeCategory::GUIElement)
		baseType = "GUIElement";
	else
		baseType = "ScriptObject";

	output << "partial class " << typeInfo.ScriptTypeName << " : " << baseType;

	output << std::endl;
	output << "\t{" << std::endl;

	output << ctors.str();
	output << properties.str();
	output << events.str();
	output << methods.str();
	output << interops.str();

	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);

	return output.str();
}

std::string generateCSStruct(StructInfo& input)
{
	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(input.documentation, "\t");

	output << "\t[StructLayout(LayoutKind.Sequential), SerializeObject]\n";

	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string scriptName = NativeToScriptTypeMap[input.name].ScriptTypeName;
	output << "partial struct " << scriptName;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto& entry : input.ctors)
	{
		bool isParameterless = entry.params.size() == 0;
		if (isParameterless) // Parameterless constructors not supported on C# structs
		{
			output << "\t\t/// <summary>Initializes the struct with default values.</summary>" << std::endl;
			output << "\t\tpublic static " << scriptName << " Default(";
		}
		else
		{
			output << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");
			output << "\t\tpublic " << scriptName << "(";
		}

		for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
		{
			const VarInfo& paramInfo = *I;

			TypeMappingInformation typeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

			if (!isValidStructType(typeInfo, paramInfo.flags))
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}

			
			if(!paramInfo.defaultValueType.empty() && !isFlagsEnum(paramInfo.flags))
			{
				// We don't generate parameters that have complex default values (as they're not supported in C#).
				// Instead the post-processor has generated different versions of this method, so we can just skip
				// such parameters
				continue;
			}

			output << typeInfo.ScriptTypeName << " " << paramInfo.name;

			if (!paramInfo.defaultValue.empty())
				output << " = " << generateCSDefaultValueAssignment(paramInfo);

			if ((I + 1) != entry.params.end())
				output << ", ";
		}

		output << ")" << std::endl;
		output << "\t\t{" << std::endl;

		std::string thisPtr;
		if (isParameterless)
		{
			output << "\t\t\t" << scriptName << " value = new " << scriptName << "();" << std::endl;
			thisPtr = "value";
		}
		else
			thisPtr = "this";

		for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
		{
			const VarInfo& fieldInfo = *I;

			TypeMappingInformation typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

			if (!isValidStructType(typeInfo, fieldInfo.flags))
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}

			std::string fieldName = fieldInfo.name;
			
			auto iterFind = entry.fieldAssignments.find(fieldInfo.name);
			if (iterFind != entry.fieldAssignments.end())
			{
				std::string paramName = iterFind->second;
				output << "\t\t\t" << thisPtr << "." << fieldName << " = " << paramName << ";" << std::endl;
			}
			else
			{
				std::string defaultValue;
				if (!fieldInfo.defaultValue.empty())
					defaultValue = generateCSDefaultValueAssignment(fieldInfo);
				else
					defaultValue = getDefaultValue(fieldInfo.typeName, fieldInfo.flags, typeInfo);

				output << "\t\t\t" << thisPtr << "." << fieldName << " = " << defaultValue << ";" << std::endl;
			}
		}

		if (isParameterless)
		{
			output << std::endl;
			output << "\t\t\treturn value;" << std::endl;
		}

		output << "\t\t}" << std::endl;
		output << std::endl;
	}

	if(!input.baseClass.empty())
	{
		TypeMappingInformation baseTypeInfo = getTypeInfo(input.baseClass, 0);
		StructInfo* baseStructInfo = findStructInfo(input.baseClass);
		if (baseStructInfo != nullptr)
		{
			// GetBase()
			output << "\t\t///<summary>\n";
			output << "\t\t/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.\n";
			output << "\t\t///</summary>\n";
			output << "\t\tpublic " << baseTypeInfo.ScriptTypeName << " GetBase()\n";
			output << "\t\t{\n";
			output << "\t\t\t" << baseTypeInfo.ScriptTypeName << " value;\n";

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
			{
				const FieldInfo& fieldInfo = *I;
				output << "\t\t\tvalue." << fieldInfo.name << " = " << fieldInfo.name << ";\n";
			}

			output << "\t\t\treturn value;\n";
			output << "\t\t}\n";
			output << "\n";

			// SetBase()
			output << "\t\t///<summary>\n";
			output << "\t\t/// Assigns values to a subset of fields of this struct. This subset usually contains common field shared with \n";
			output << "\t\t/// another struct.\n";
			output << "\t\t///</summary>\n";
			output << "\t\tpublic void SetBase(" << baseTypeInfo.ScriptTypeName << " value)\n";
			output << "\t\t{\n";

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
			{
				const FieldInfo& fieldInfo = *I;
				output << "\t\t\t" << fieldInfo.name << " = value." << fieldInfo.name << ";\n";
			}

			output << "\t\t}\n";
			output << "\n";
		}
	}

	for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
	{
		const FieldInfo& fieldInfo = *I;

		TypeMappingInformation typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

		if (!isValidStructType(typeInfo, fieldInfo.flags))
		{
			outs() << "Error: Invalid field type found in struct \"" << scriptName << "\" for field \"" << fieldInfo.name << "\". Skipping.\n";
			continue;
		}

		output << CommentParser::GenerateXMLComments(fieldInfo.documentation, "\t\t");
		output << generateCSStyleAttributes(fieldInfo.style, typeInfo, fieldInfo.flags, true);

		if ((fieldInfo.style.flags & (int)StyleFlags::ForceHide) != 0)
			output << "\t\t[HideInInspector]" << std::endl;

		output << "\t\tpublic ";

		output << typeInfo.ScriptTypeName;
		if (isArrayOrVector(fieldInfo.flags))
			output << "[]";

		output << " ";
		output << fieldInfo.name;

		output << ";" << std::endl;
	}

	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);
	return output.str();
}

std::string generateCSEnum(EnumInfo& input)
{
	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(input.documentation, "\t");
	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";

	output << "enum " << input.scriptName;

	if (!input.explicitType.empty())
		output << " : " << input.explicitType;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		if (I != input.entries.begin())
			output << ",\n";

		const EnumEntryInfo& entryInfo = I->second;

		output << CommentParser::GenerateXMLComments(entryInfo.Documentation, "\t\t");
		output << "\t\t" << entryInfo.ScriptName;
		output << " = ";
		output << entryInfo.Value;
	}
	
	output << "\n";
	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);
	return output.str();
}

std::string generateXMLParamInfo(const VarInfo& varInfo, const CommentEntry& methodDoc, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<param name=\"" << escapeXML(varInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(varInfo.typeName, varInfo.flags).ScriptTypeName) << "\">\n";

	auto iterFind = std::find_if(methodDoc.params.begin(), methodDoc.params.end(), 
		[&varName = varInfo.name](const CommentParameterEntry& entry) { return varName == entry.Name; });
	if (iterFind != methodDoc.params.end() && !iterFind->comments.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(iterFind->comments) << "</doc>\n";

	output << indent << "</param>\n";
	return output.str();
}

std::string generateXMLFieldInfo(const FieldInfo& fieldInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<field name=\"" << escapeXML(fieldInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(fieldInfo.typeName, fieldInfo.flags).ScriptTypeName) << "\">\n";

	// TODO - Generate inspector visibility
	if(!fieldInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(fieldInfo.documentation.brief) << "</doc>\n";

	output << indent << "</field>\n";
	return output.str();
}

std::string generateXMLMethodInfo(const MethodInfo& methodInfo, const std::string& indent, bool ctor)
{
	std::stringstream output;

   std::string isStaticStr = "false";
   bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
   if(!ctor && isStatic)
	   isStaticStr = "true";

	if(!ctor)
	{
		output << indent << "<method native=\"" << escapeXML(methodInfo.sourceName) << "\" script=\"" << 
			escapeXML(methodInfo.scriptName) << "\" static=\"" << isStaticStr << "\">\n";
	}
	else
		output << indent << "<ctor>\n";

	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.paramInfos)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	if(!ctor && !methodInfo.returnInfo.typeName.empty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags).ScriptTypeName) << "\">\n";

		if (!methodInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	if(!ctor)
		output << indent << "</method>\n";
	else
		output << indent << "</ctor>\n";

	return output.str();
}

std::string generateXMLMethodInfo(const SimpleConstructorInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<ctor>\n";
	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.params)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	output << indent << "</ctor>\n";
	return output.str();
}

std::string generateXMLPropertyInfo(const PropertyInfo& propertyInfo, const std::string& indent)
{
	std::string staticStr = propertyInfo.isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<property name=\"" << escapeXML(propertyInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(propertyInfo.type, propertyInfo.typeFlags).ScriptTypeName) << 
		"\" getter=\"" << escapeXML(propertyInfo.getter) << "\" setter=\"" << escapeXML(propertyInfo.setter) << 
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if(!propertyInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(propertyInfo.documentation.brief) << "</doc>\n";

	output << indent << "</property>\n";
	return output.str();
}

std::string generateXMLEventInfo(const MethodInfo& eventInfo, const std::string& indent)
{
   bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
   std::string staticStr = isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<event native=\"" << escapeXML(eventInfo.sourceName) << "\" script=\"" << escapeXML(eventInfo.scriptName) << 
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if (!eventInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(eventInfo.documentation.brief) << "</doc>\n";

	for(auto& param : eventInfo.paramInfos)
		output << generateXMLParamInfo(param, eventInfo.documentation, indent + "\t");

	if(!eventInfo.returnInfo.typeName.empty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(getTypeInfo(eventInfo.returnInfo.typeName, eventInfo.returnInfo.flags).ScriptTypeName) << "\">\n";

		if (!eventInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(eventInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	output << indent << "</event>\n";
	return output.str();
}

std::string generateXMLEnum(EnumInfo& input, const std::string& indent)
{
	std::stringstream output;

	output << indent << "<enum native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(input.scriptName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";
	
	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		const EnumEntryInfo& entryInfo = I->second;

	   output << indent << "\t<enumentry native=\"" << escapeXML(entryInfo.NativeName) << "\" script=\"" << escapeXML(entryInfo.ScriptName) << "\">\n";
	   if (!entryInfo.Documentation.brief.empty())
		   output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(entryInfo.Documentation.brief) << "</doc>\n";
	   output << indent << "\t</enumentry>\n";
	}
	
	output << indent << "</enum>\n";
	return output.str();
}

std::string generateXMLStruct(StructInfo& input, const std::string& indent)
{
	std::stringstream output;

	TypeMappingInformation& typeInfo = NativeToScriptTypeMap[input.name];

	output << indent << "<struct native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctors)
		output << generateXMLMethodInfo(entry, indent + "\t");

	for(auto& entry : input.fields)
	  output << generateXMLFieldInfo(entry, indent + "\t");
	
	output << indent << "</struct>\n";
	return output.str();
}

std::string generateXMLClass(ClassInfo& input, bool editor, const std::string& indent)
{
	std::stringstream output;

	TypeMappingInformation& typeInfo = NativeToScriptTypeMap[input.name];

	output << indent << "<class native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctorInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if(isValidAPI(entry.api, editor) && !interopOnly)
			output << generateXMLMethodInfo(entry, indent + "\t", true);
	}

	for(auto& entry : input.methodInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);

		if(isValidAPI(entry.api, editor) && !interopOnly && !isProperty)
			output << generateXMLMethodInfo(entry, indent + "\t", isConstructor);
	}

   for(auto& entry : input.propertyInfos)
   {
		if(isValidAPI(entry.api, editor))
			output << generateXMLPropertyInfo(entry, indent + "\t");
   }

   for(auto& entry : input.eventInfos)
   {
	   bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
	   bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

	  if(!isCallback && !isInternal)
		  output << generateXMLEventInfo(entry, indent + "\t");
   }
	
	output << indent << "</class>\n";
	return output.str();
}

void cleanAndPrepareFolder(const StringRef& folder)
{
	if (sys::fs::exists(folder))
	{
		std::error_code ec;
		for (sys::fs::directory_iterator file(folder, ec), fileEnd; file != fileEnd && !ec; file.increment(ec))
			sys::fs::remove(file->path());
	}

	sys::fs::create_directories(folder);
}

std::ofstream createFile(const std::string& filename, StringRef outputFolder)
{
	std::string relativePath = "/" + filename;
	StringRef filenameRef(relativePath.data(), relativePath.size());

	SmallString<128> filepath = outputFolder;
	sys::path::append(filepath, filenameRef);

	std::ofstream output;
	output.open(filepath.str().str(), std::ios::out);

	return output;
}

void generateMappingXMLFile(bool editor, const std::string& outputFolder)
{
	std::stringstream body;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& enumInfos = fileInfo.second.enumInfos;
		for (auto& entry : enumInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLEnum(entry, "\t");
		}

		auto& structInfos = fileInfo.second.structInfos;
		for (auto& entry : structInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLStruct(entry, "\t");
		}


		auto& classInfos = fileInfo.second.classInfos;
		for (auto& entry : classInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLClass(entry, editor, "\t");
		}
	}

	std::ofstream output = createFile("info.xml", outputFolder);

	output << "<?xml version='1.0' encoding='UTF-8' standalone='no'?>\n";
	output << "<entries>\n";
	output << body.str();
	output << "</entries>\n";
	output.close();
}

void generateLookupFile(const std::string& tableName, ::TypeCategory type, bool editor,
	const std::string& engineOutputFolder, const std::string& editorOutputFolder)
{
	StringRef cppOutputFolder = editor ? editorOutputFolder : engineOutputFolder;

	std::stringstream body;
	std::stringstream includes;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& classInfos = fileInfo.second.classInfos;
		if (classInfos.empty())
			continue;

		if(fileInfo.second.inEditor != editor)
			continue;

		bool hasType = false;
		for (auto& classInfo : classInfos)
		{
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];
			if (typeInfo.TypeCategory != type)
				continue;

			includes << generateCppApiCheckBegin(classInfo.api);
			includes << "#include \"" << getRelativeTo(typeInfo.NativeFile, cppOutputFolder) << "\"" << std::endl;
			includes << generateApiCheckEnd(classInfo.api);

			std::string interopClassName = getScriptInteropType(classInfo.name);
			body << generateCppApiCheckBegin(classInfo.api);
			body << "\t\tADD_ENTRY(" << classInfo.name << ", " << interopClassName << ")" << std::endl;
			body << generateApiCheckEnd(classInfo.api);

			hasType = true;
		}

		if(hasType)
			includes << "#include \"BsScript" + fileInfo.first + ".generated.h\"" << std::endl;
	}

	std::string prefix = editor ? "Editor" : "";
	std::ofstream output = createFile("Bs" + prefix + tableName + "Lookup.generated.h", cppOutputFolder);

	// License/copyright header
	output << generateFileHeader(editor);

	output << "#pragma once" << std::endl;
	output << std::endl;

	output << "#include \"Serialization/Bs" << tableName << "Lookup.h\"" << std::endl;
	output << "#include \"Reflection/BsRTTIType.h\"" << std::endl;
	output << includes.str();

	output << std::endl;

	output << "namespace " << (editor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
	output << "{" << std::endl;
	output << "\tLOOKUP_BEGIN(" << prefix << tableName << ")" << std::endl;

	output << body.str();

	output << "\tLOOKUP_END" << std::endl;
	output << "}" << std::endl;

	output << "#undef LOOKUP_BEGIN" << std::endl;
	output << "#undef ADD_ENTRY" << std::endl;
	output << "#undef LOOKUP_END" << std::endl;

	output.close();
}

void generateAll(StringRef cppEngineOutputFolder, StringRef cppEditorOutputFolder, StringRef csEngineOutputFolder, 
	StringRef csEditorOutputFolder, bool genEditor)
{
	cleanAndPrepareFolder(cppEngineOutputFolder);
	cleanAndPrepareFolder(csEngineOutputFolder);

	if(genEditor)
	{
		cleanAndPrepareFolder(cppEditorOutputFolder);
		cleanAndPrepareFolder(csEditorOutputFolder);
	}

	//{
	//	std::string relativePath = "scriptBindings.timestamp";
	//	StringRef filenameRef(relativePath.data(), relativePath.size());

	//	SmallString<128> filepath = cppOutputFolder;
	//	sys::path::append(filepath, filenameRef);

	//	std::ofstream output;
	//	output.open(filepath.str(), std::ios::out);

	//	std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	//		std::chrono::system_clock::now().time_since_epoch());
	//	output << std::to_string(ms.count());
	//	output.close();
	//}

	// Generate H
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << generateCppHeaderOutput(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			StructInfo& structInfo = *I;
			body << generateCppStructHeader(structInfo);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.inEditor ? cppEditorOutputFolder : cppEngineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.h", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "#pragma once" << std::endl;
		output << std::endl;

		// Output includes
		for (auto& include : fileInfo.second.referencedHeaderIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		// Output forward declarations
		for (auto& decl : fileInfo.second.forwardDeclarations)
		{
			for (auto& nsEntry : decl.ns)
				output << "namespace " << nsEntry << " { ";
			
			if (decl.templParams.size() > 0)
			{
				output << "template<";

				for (int i = 0; i < (int)decl.templParams.size(); ++i)
				{
					if (i != 0)
						output << ", ";

					output << decl.templParams[i].type << " T" << std::to_string(i);
				}

				output << "> ";
			}

			if (decl.isStruct)
				output << "struct " << decl.name << ";";
			else
				output << "class " << decl.name << ";";

			for (auto& nsEntry : decl.ns)
				output << " }";
			
			output << "\n";
		}

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate CPP
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << generateCppSourceOutput(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			body << generateCppStructSource(*I);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.inEditor ? cppEditorOutputFolder : cppEngineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.cpp", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		// Output includes
		for (auto& include : fileInfo.second.referencedSourceIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate CS
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;
		auto& enumInfos = fileInfo.second.enumInfos;

		if (classInfos.empty() && structInfos.empty() && enumInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << generateCSClass(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			body << generateCSStruct(*I);

			if ((I + 1) != structInfos.end() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = enumInfos.begin(); I != enumInfos.end(); ++I)
		{
			body << generateCSEnum(*I);

			if ((I + 1) != enumInfos.end())
				body << std::endl;
		}

		StringRef csOutputFolder = fileInfo.second.inEditor ? csEditorOutputFolder : csEngineOutputFolder;
		std::ofstream output = createFile(fileInfo.first + ".generated.cs", csOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "using System;" << std::endl;
		output << "using System.Runtime.CompilerServices;" << std::endl;
		output << "using System.Runtime.InteropServices;" << std::endl;

		if (fileInfo.second.inEditor)
			output << "using " << sFrameworkCsNs << ";" << std::endl;

		output << std::endl;

		if (!fileInfo.second.inEditor)
			output << "namespace " << sFrameworkCsNs << "\n";
		else
			output << "namespace " << sEditorCsNs << "\n";

		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate builtin component lookup file
	generateLookupFile("BuiltinComponent", ::TypeCategory::Component, false, cppEngineOutputFolder.str(), cppEditorOutputFolder.str());

	// Generate C++ reflectable type lookup files
	generateLookupFile("BuiltinReflectableTypes", ::TypeCategory::ReflectableClass, false, cppEngineOutputFolder.str(), cppEditorOutputFolder.str());
	generateLookupFile("BuiltinReflectableTypes", ::TypeCategory::ReflectableClass, true, cppEngineOutputFolder.str(), cppEditorOutputFolder.str());

	// Generate XML lookup
	generateMappingXMLFile(false, csEngineOutputFolder.str());

	if(genEditor)
		generateMappingXMLFile(true, csEditorOutputFolder.str());
}
