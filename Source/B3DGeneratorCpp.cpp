#include "B3DCommon.h"
#include <chrono>

#include "B3DGeneratorUtility.h"
#include "B3DParserUtility.h"
#include "B3DTypeLookup.h"

#pragma region C++ Generation Helpers

/** Returns true if the provided type is passed as value type to an internal method parameter. */
static bool IsInternalMethodParameterValueType(const VariableTypeInformation& typeInformation)
{
	if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
		return false;

	switch(typeInformation.TypeCategory)
	{
	case VariableTypeCategory::SharedPointer:
	case VariableTypeCategory::ResourceHandle: 
	case VariableTypeCategory::GameObjectHandle:
	case VariableTypeCategory::MonoObject:
	case VariableTypeCategory::MonoReflectionType:
		return false;
	default: 
		return true;
	}
}

/**
 * Returns a qualified name for the C++ interop type representing the type in @p typeInformation.
 *
 * @param	typeInformation				Information about the native type to generate the interop type for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 * @param	isGeneratingField			When true, it implies the interop type will be used for generating a field member in class or struct. If false it implies we're generating it for a parameter or a return value.
 */
static std::string GetCppInteropQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isGeneratingField = false)
{
	const bool isOutputParameter = typeInformation.IsOutputParameter(typeMappingInformation) && !isGeneratingField;
	if (typeInformation.IsArrayOrVector())
		return isOutputParameter ? "MonoArray**" : "MonoArray*";

	const std::string& typeName = typeInformation.GetFirstWrappedOrSelfTypeName();

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
		return isOutputParameter ? typeName + "*" : typeName;
	case ExportedClassTypeCategory::Enum:
		if (typeInformation.TypeCategory == VariableTypeCategory::Flags && isGeneratingField)
			return "Flags<" + typeName + ">";
		else
			return isOutputParameter ? typeName + "*" : typeName;
	case ExportedClassTypeCategory::Struct:
		if(typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
		{
			const std::string structInteropType = GetStructInteropTypeName(typeName);
			return isGeneratingField ? structInteropType : structInteropType + "*";
		}

		return isGeneratingField ? typeName : typeName + "*";
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::ConstCharString:
	case ExportedClassTypeCategory::Path:
		return isOutputParameter ? "MonoString**" : "MonoString*";
	case ExportedClassTypeCategory::MonoReflectionType:
		return isOutputParameter ? "MonoReflectionType**" : "MonoReflectionType*";
	default: // Class, resource, component or ScriptObject
		return isOutputParameter ? "MonoObject**" : "MonoObject*";
	}
}

/**
 * Returns a type name for the C++ native type representing the type in @p typeInformation.
 *
 * @param	typeInformation					Information about the native type to generate the type name for.
 * @param	typeMappingInformation			Mapping of the provided type in script.
 * @param	isVariable						If true, the generated type is expected to be used for local variable storage (e.g. qualifiers such as `const &` will be dropped.). Otherwise it's expected to be used for parameters or template arguments.
 */
static std::string GetCppNativeQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isVariable = true)
{
	const std::string& typeName = typeInformation.GetFirstWrappedOrSelfTypeName();

	std::stringstream output;
	if ((!isVariable || typeInformation.TypeCategory == VariableTypeCategory::ConstCharString) && typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsConst))
		output << "const ";

	if(typeInformation.TypeCategory == VariableTypeCategory::Vector)
		output << "Vector<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ">";
	else if(typeInformation.TypeCategory == VariableTypeCategory::TInlineArray)
		output << "TInlineArray<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ", " + std::to_string(typeInformation.ArraySize) + ">";
	else if(typeInformation.TypeCategory == VariableTypeCategory::TArray)
		output << "TArray<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ">";
	else if(typeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
		output << "TAsyncOp<" + GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false) + ">";
	else if(typeInformation.TypeCategory == VariableTypeCategory::Array)
		output << GetCppNativeQualifiedTypeName(typeInformation.AssertGetUnderlyingType(), typeMappingInformation, false);
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
		output << "TResourceHandle<" + typeName + ">";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GameObject || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::SceneObject || typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		output << "TGameObjectHandle<" + typeName + ">";
	else if(typeMappingInformation.IsClassType())
	{
		if(isVariable || typeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
			output << "SPtr<" + typeName + ">";
		else
			output << typeName;
	}
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::String)
		output << "String";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString)
		output << "WString";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::ConstCharString)
		output << "char";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
		output << "Path";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && typeInformation.TypeCategory == VariableTypeCategory::Flags)
		output << "Flags<" + typeName + ">";
	else if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::MonoObject || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::MonoReflectionType)
		output << typeName + "*";
	else
		output << typeName;

	if (!isVariable || typeInformation.TypeCategory == VariableTypeCategory::ConstCharString)
	{
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			output << "*";
		else if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
			output << "&";
	}

	return output.str();
}

/** Same as GetCppQualifiedTypeName, except the only type information used is the type name. */
static std::string GetCppNativeQualifiedTypeName(const std::string& typeName, const TypeMappingInformation& typeMappingInformation, bool wrapClassTypesInSharedPointer = true)
{
	VariableTypeInformation typeInformation;
	typeInformation.TypeName = typeName;

	return GetCppNativeQualifiedTypeName(typeInformation, typeMappingInformation, wrapClassTypesInSharedPointer);
}

/** Wraps the provided type in const& if necessary. Primarily required when passing the type as parameter or return value. @p type is returned by GetCppNativeQualifiedTypeName(). */
static std::string GetParameterQualifiedType(const TypeMappingInformation& typeMappingInformation, const std::string& type)
{
	if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement)
		return type; // Return as-is, as it's passed by pointer and the * is already encoded in the name

	return "const " + type + "&";
}

/** Returns the name of the templated base class to use for the script object wrapper implementation. */
static std::string GetWrapperTemplatedBaseClass(const TypeMappingInformation& typeMappingInformation, bool isModule)
{
	if(isModule)
		return "TScriptTypeDefinition";

	if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		return "TScriptResourceWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		return "TScriptGameObjectWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
		return "TScriptGUIElementWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
		return "TScriptReflectableWrapper";
	else // Class
		return "TScriptNonReflectableWrapper";
}

/** Returns the root base class to use for the script object wrapper implementation. This is the class that root base script interop wrappers should inherit from. */
static std::string GetWrapperRootBaseClass(const std::string& nativeClassName, const TypeMappingInformation& typeMappingInformation, bool withTemplateArguments)
{
	if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		return "ScriptResourceWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
		return "ScriptGameObjectWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
		return "ScriptGUIElementWrapper";
	else if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass)
		return "ScriptReflectableWrapper";
	else // Class
	{
		if(withTemplateArguments)
			return "TScriptNonReflectableWrapperBase<" + nativeClassName + ">";
		else
			return "TScriptNonReflectableWrapperBase";
	}
}

/**
 * Parses a type containing generic arguments, and returns the generic arguments.
 * e.g. for MyType<Arg1, Arg2<Arg3>> returns an array containing two entries [Arg1, Arg2<Arg3>].
 */
static SmallVector<StringRef, 2> ParseGenericArguments(const StringRef& type)
{
	SmallVector<StringRef, 2> genericArguments;

	uint32_t level = 0;
	size_t start = 0;
	for(size_t i = 0; i < type.size(); ++i)
	{
		if(type[i] == '<')
			++level;
		else if(type[i] == '>')
			--level;
		else if(type[i] == ',' && level == 0)
		{
			StringRef genericArgument = type.substr(start, i - start);
			genericArgument = genericArgument.trim();

			genericArguments.push_back(genericArgument);

			start = i + 1;
		}
	}

	if(start < type.size())
	{
		StringRef genericArgument = type.substr(start);
		genericArgument = genericArgument.trim();

		genericArguments.push_back(genericArgument);
	}

	return genericArguments;
}

/**
 * Returns a type name for the Mono thunk signature lookup, representing the type in @p typeInformation.
 *
 * @param	typeInformation					Information about the native type to generate the type name for.
 * @param	typeMappingInformation			Mapping of the provided type in script.
 */
static std::string GetInteropThunkSignatureQualifiedTypeName(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	std::function<std::string(const std::string&)> fnConvertType = [&fnConvertType](const StringRef& input) -> std::string
	{
		std::string typeName;

		// Generic types require `X after their name, with number of arguments following
		size_t templateStartPosition = input.find_first_of('<');
		if(templateStartPosition != StringRef::npos)
		{
			const StringRef genericType = input.substr(0, templateStartPosition);
			const StringRef unparsedArguments = input.substr(templateStartPosition + 1, input.size() - templateStartPosition - 2);

			SmallVector<StringRef, 2> parsedArguments = ParseGenericArguments(unparsedArguments);
			const uint32_t argumentCount = (uint32_t)parsedArguments.size();

			std::stringstream typeNameStream;
			typeNameStream << genericType.str() << "`" << argumentCount << "<";

			for(uint32_t argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
			{
				typeNameStream << fnConvertType(parsedArguments[argumentIndex].str());

				if((argumentIndex + 1) < argumentCount)
					typeNameStream << ", ";
			}

			typeNameStream << ">";
			typeName = typeNameStream.str();
		}
		else
			typeName = input.str();

		if(typeName == "float")
			typeName = "single";

		return typeName;
	};

	StringRef scriptTypeName(typeMappingInformation.ScriptTypeName.data(), typeMappingInformation.ScriptTypeName.length());
	scriptTypeName = scriptTypeName.trim();

	std::stringstream output;
	output << fnConvertType(typeMappingInformation.ScriptTypeName);

	if (typeInformation.IsArrayOrVector())
		output << "[]";

	if (typeInformation.IsOutputParameter(typeMappingInformation) || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
		output << "&";

	return output.str();
}

/**
 * Generates a line of code that retrieves the native object from the script wrapper object.
 *
 * @param typeInformation			Information about the native type the native object represents.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @param variableName				Name of the variable containing the script wrapper object, to access the native object through.
 * @param requiresStrongReference	If true the returned object will be wrapped in a type that holds a strong reference to it (e.g. shared pointer or handle). If false it will be returned
 *									as a raw pointer.
 * @return							String containing the C++ line of code to retrieve the native object from @p variableName.
 */
static std::string GenerateGetNativeObjectCallLine(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& variableName, bool requiresStrongReference = true)
{
	const bool isPassingAsResourceReference = typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);

	const std::string& nativeTypeName = typeInformation.GetLastWrappedOrSelfTypeName();

	std::stringstream output;
	if(requiresStrongReference)
	{
		if(typeMappingInformation.IsClassType())
			output << "std::static_pointer_cast<" << nativeTypeName << ">(" << variableName << "->GetBaseNativeObjectAsShared())";
		else if(typeMappingInformation.IsHandleType())
		{
			if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
			{
				if(isPassingAsResourceReference)
					output << "B3DStaticResourceCast<" << nativeTypeName << ">(" << variableName << "->GetNativeObject())";
				else
					output << "B3DStaticResourceCast<" << nativeTypeName << ">(" << variableName << "->GetBaseNativeObjectAsHandle())";
			}
			else // Game object
				output << "B3DStaticGameObjectCast<" << nativeTypeName << ">(" << variableName << "->GetBaseNativeObjectAsHandle())";
		}
		else // Must be GUI element type
		{
			assert(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement);
			output << "static_cast<" << nativeTypeName << "*>(" << variableName << "->GetNativeObject())";
		}
	}
	else
		output << "static_cast<" << nativeTypeName << "*>(" << variableName << "->GetNativeObject())";
	
	return output.str();
}

/**
 * Returns an argument that can be used for call into a native method. The argument is expected to have been received through
 * an internal interop call.
 *
 * @param	methodInfo				Information about the method being called.
 * @param	argumentName			Name of the argument.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to retrieves the appropriate argument type from the expected internal argument storage type.
 */
static std::string GetArgumentForInternalToNativeCall(const MethodInfo& methodInfo, const std::string& argumentName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	// Performs the conversion for arguments whose source type is either a value type or pointer type.
	auto fnGetPlainArgument = [&methodInfo, &argumentName, &typeInformation](bool isInputPointerType)
	{
		if (IsInternalMethodParameterValueType(typeInformation))
			return (isInputPointerType ? "*" : "") + argumentName;

		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return (isInputPointerType ? "" : "&") + argumentName;

		errs() << "Error: Invalid type for method argument " << argumentName << " on method " << methodInfo.NativeName << ".\n";
		return argumentName;
	};

	enum class HandleType
	{
		ResourceHandle,
		GameObjectHandle
	};

	// Performs the conversion for arguments whose source type is a handle type.
	auto fnGetHandleArgument = [&argumentName, &typeInformation](HandleType handleType)
	{
		if (handleType == HandleType::ResourceHandle)
		{
			assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::ResourceHandle || typeInformation.TypeCategory == VariableTypeCategory::General);

			if (typeInformation.TypeCategory == VariableTypeCategory::ResourceHandle)
				return argumentName;
		}
		else
		{
			assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::GameObjectHandle || typeInformation.TypeCategory == VariableTypeCategory::General);

			if (typeInformation.TypeCategory == VariableTypeCategory::GameObjectHandle)
				return argumentName;
		}

		if (typeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
			return argumentName + ".GetInternalPtr()";

		assert(typeInformation.TypeCategory == VariableTypeCategory::General);
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return argumentName + ".get()";

		return "*" + argumentName;
	};

	if(typeInformation.IsArrayOrVector())
	{
		return fnGetPlainArgument(false);
	}

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
	case ExportedClassTypeCategory::Enum: // Input type is either value or pointer depending if output or not
		return fnGetPlainArgument(typeInformation.IsOutputParameter(typeMappingInformation));
	case ExportedClassTypeCategory::Struct: // Input type is always a pointer
		return fnGetPlainArgument(!typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));
	case ExportedClassTypeCategory::String: // Input type is always a value
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
		return fnGetPlainArgument(false);
	case ExportedClassTypeCategory::GUIElement: // Input type is always a pointer
	case ExportedClassTypeCategory::MonoObject:
	case ExportedClassTypeCategory::MonoReflectionType:
	case ExportedClassTypeCategory::ConstCharString:
		return fnGetPlainArgument(true);
	case ExportedClassTypeCategory::Component: // Input type is always a handle
	case ExportedClassTypeCategory::SceneObject:
	case ExportedClassTypeCategory::GameObject:
		return fnGetHandleArgument(HandleType::GameObjectHandle);
	case ExportedClassTypeCategory::Resource:
		return fnGetHandleArgument(HandleType::ResourceHandle);
	case ExportedClassTypeCategory::Class: // Input type is always a SPtr
	case ExportedClassTypeCategory::ReflectableClass:
	case ExportedClassTypeCategory::IReflectable:
	{
		assert(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer || typeInformation.TypeCategory == VariableTypeCategory::General);

		if(typeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
			return argumentName;

		if(typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return argumentName + ".get()";

		return "*" + argumentName;
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

/*
 * Returns an argument that can be used for call into a thunk. The argument is expected to have been received through a native event, translated to an interop type.
 *
 * @param	methodInfo				Information about the event that is being triggered.
 * @param	argumentName			Name of the argument.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to retrieves the appropriate argument type from the event parameter argument type.
 */
static std::string GetArgumentForInteropEventToThunkCall(const MethodInfo& methodInfo, const std::string& argumentName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	if(typeInformation.IsArrayOrVector())
	{
		// Always passed as pointer (MonoArray*), input will always be a pointer
		return argumentName;
	}

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive:
	case ExportedClassTypeCategory::Enum: // Always passed as value type, input can be either pointer or ref/value type
	{
		if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + argumentName;

		return argumentName;
	}
	case ExportedClassTypeCategory::Struct: // Always passed as pointer, input will be a pointer (boxed struct as MonoObject*)
	case ExportedClassTypeCategory::MonoObject: // Always passed as a pointer, input will always be a pointer (MonoObject*)
	case ExportedClassTypeCategory::MonoReflectionType:
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::ConstCharString:
	case ExportedClassTypeCategory::Path:
	case ExportedClassTypeCategory::GameObject:
	case ExportedClassTypeCategory::Component:
	case ExportedClassTypeCategory::SceneObject:
	case ExportedClassTypeCategory::Resource:
	case ExportedClassTypeCategory::Class:
	case ExportedClassTypeCategory::ReflectableClass:
	case ExportedClassTypeCategory::IReflectable:
		return argumentName;
	default: // Some object type
		errs() << "Error: Invalid type for method argument " << argumentName << " on method " << methodInfo.NativeName << ".\n";
		return argumentName;
	}
}

/*
 * Adds appropriate qualifiers to convert a type access (such as field or method call return value) into a default storage type for the mapped type category.
 *
 * @param	access					Field name or method call.
 * @param	typeInformation			Information about the native type the argument represents.
 * @param	typeMappingInformation	Mapping of the provided argument type in script.
 * @return							Code to converts the field or return value into the expected internal argument storage type.
 */
static std::string GetReturnValueForNativeCall(const std::string& access, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	const VariableTypeInformation& arrayElementType = typeInformation.IsArrayOrVector() ? typeInformation.AssertGetUnderlyingType() : typeInformation;
	const VariableTypeInformation& underlyingType = arrayElementType.TypeCategory == VariableTypeCategory::AsyncOp ? arrayElementType.AssertGetUnderlyingType() : arrayElementType;

	switch (typeMappingInformation.TypeCategory)
	{
	case ExportedClassTypeCategory::Primitive: // Always passed as value type, input can be either pointer or ref/value type
	case ExportedClassTypeCategory::Enum:
	case ExportedClassTypeCategory::String:
	case ExportedClassTypeCategory::WString:
	case ExportedClassTypeCategory::Path:
	case ExportedClassTypeCategory::Struct:
	{
		if (underlyingType.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + access;

		return access;
	}
	case ExportedClassTypeCategory::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ExportedClassTypeCategory::MonoReflectionType:
	case ExportedClassTypeCategory::GUIElement:
	case ExportedClassTypeCategory::ConstCharString:
			return access;
	case ExportedClassTypeCategory::Component: // Always passed as a handle, input must be a handle
	{
		if (underlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		return access;
	}
	case ExportedClassTypeCategory::GameObject:
	case ExportedClassTypeCategory::SceneObject:
		if (underlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		return access;
	case ExportedClassTypeCategory::Resource:
		if (underlyingType.TypeCategory != VariableTypeCategory::ResourceHandle)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		return access;
	case ::ExportedClassTypeCategory::Class: // Passed as a shared pointer or value type, input can be a shared pointer, pointer, reference or value type
	case ::ExportedClassTypeCategory::ReflectableClass:
	case ::ExportedClassTypeCategory::IReflectable:
	{
		if (underlyingType.TypeCategory == VariableTypeCategory::SharedPointer)
			return access;

		if(underlyingType.TypeCategory != VariableTypeCategory::General)
		{
			errs() << "Error: Unsure how to provide \"" << access << "\" to interop as a return value.\".\n";
			return access;
		}

		if (underlyingType.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
			return "*" + access;

		return access;
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

/** Generates a check for a preprocessor conditional depending on the API the code is currently being compiled for. */
static std::string GenerateApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::Framework)
		return "#if !B3D_IS_ENGINE\n";
	else if(api == ApiFlags::Engine)
		return "#if B3D_IS_ENGINE\n";

	return "";
}

/** Ends the preprocessor conditional started by GenerateAPICheckBegin(). These calls must match 1:1. */
static std::string GenerateApiCheckEnd(ApiFlags api)
{
	if(api == ApiFlags::Framework || api == ApiFlags::Engine)
		return "#endif\n";

	return "";
}

/** Generates code that checks for native object validity, and if invalid returns from the method. */
static std::string GenerateNativeObjectValidityCheck(const ClassInfo& classInfo, const MethodInfo& methodInfo, const char* indent = "\t\t")
{
	const bool isModule = classInfo.IsFlagSet(ClassFlags::IsModule);
	const bool isSingleton = classInfo.IsFlagSet(ClassFlags::IsSingleton);
	const bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);
	const bool isCtor = methodInfo.IsFlagSet(MethodFlags::Constructor);

	std::stringstream output;
	if(!isCtor && !isStatic && !isSingleton && !isModule)
	{
		output << indent << "if(!self->IsNativeObjectValid())\n";

		if(!methodInfo.ReturnValue.TypeInformation.IsEmpty())
		{
			TypeMappingInformation returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation);
			const bool returnAsParameter = !GeneratorUtility::CanBeReturned(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);

			if(!returnAsParameter)
				output << indent << "\treturn {};\n\n";
			else
			{
				output << indent << "{\n";
				output << indent << "\t*__output = {};\n";
				output << indent << "\treturn;\n";
				output << indent << "}\n\n";
			}
		}
		else
			output << indent << "\treturn;\n\n";
	}

	return output.str();
}

/**
 * Generates the name and parameters for an internal method.
 *
 * @param classInfo				Information about the class the method belongs to.
 * @param methodInfo			Information about the method being generated.
 * @param interopThisPtrType	Interop type used for storing the interop object we're generating the method for. This may be the same as @p interopTypeName, but may be some base type.
 * @param interopTypeName		Interop type we're generating the method on. This will be used as a prefix to the method name, followed by '::'. Set to empty if generating signature for the header declaration.
 * @return						Method signature, including method name and parameters.
 */
static std::string GenerateInternalMethodSignature(const ClassInfo& classInfo, const MethodInfo& methodInfo, const std::string& interopThisPtrType, const std::string& interopTypeName)
{
	const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();
	const bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);
	const bool isCtor = methodInfo.IsFlagSet(MethodFlags::Constructor);

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.ReturnValue.TypeInformation.TypeName.empty() || isCtor)
		output << "void";
	else
	{
		const TypeMappingInformation& returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation);
		if (!GeneratorUtility::CanBeReturned(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			output << GetCppInteropQualifiedTypeName(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);
		}
	}

	output << " ";

	if (!interopTypeName.empty())
		output << interopTypeName << "::";

	output << "Internal" << methodInfo.InteropName << "(";

	if (isCtor)
	{
		output << "MonoObject* scriptObject";

		if (!methodInfo.Parameters.empty())
			output << ", ";
	}
	else if (!isStatic && !classHasGlobalSingleInstance)
	{
		output << interopThisPtrType << "* self";

		if (!methodInfo.Parameters.empty() || returnAsParameter)
			output << ", ";
	}

	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const TypeMappingInformation& parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(I->TypeInformation);
		output << GetCppInteropQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation) << " " << I->Name;

		if ((I + 1) != methodInfo.Parameters.end() || returnAsParameter)
			output << ", ";
	}

	if (returnAsParameter)
	{
		const TypeMappingInformation& returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation);
		output << GetCppInteropQualifiedTypeName(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation) << " " << "__output";
	}

	output << ")";
	return output.str();
}

/**
 * Generates the name and parameters for a method that serves as an event callback.
 *
 * @param classInfo			Information about the class the event belongs to.
 * @param eventInfo			Information about the event we're generating the callback for.
 * @param interopTypeName	Interop type we're generating the method on. This will be used as a prefix to the method name, followed by '::'. Set to empty if generating signature for the header declaration.
 * @return					Method signature, including method name and parameters.
 */
static std::string GenerateEventCallbackSignature(const ClassInfo& classInfo, const MethodInfo& eventInfo, const std::string& interopTypeName)
{
	const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();
	const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);

	std::stringstream output;

	if ((isStatic || classHasGlobalSingleInstance) && interopTypeName.empty())
		output << "static ";

	output << "void ";
	
	if (!interopTypeName.empty())
		output << interopTypeName << "::";
	
	output << eventInfo.InteropName << "(";

	int parameterIndex = 0;
	for (auto I = eventInfo.Parameters.begin(); I != eventInfo.Parameters.end(); ++I)
	{
		const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(I->TypeInformation);
		output << GetCppNativeQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation, false);

		output << " p" << parameterIndex;

		if (I->TypeInformation.TypeCategory == VariableTypeCategory::Array)
			output << "[" << I->TypeInformation.ArraySize << "]";

		if ((I + 1) != eventInfo.Parameters.end())
			output << ", ";

		parameterIndex++;
	}

	output << ")";
	return output.str();
}

/**
 * Generates the type definition and a static field holding a thunk for a particular event.
 *
 * @param eventInfo							Information about the event we're generating the thunk for.
 * @param classHasGlobalSingleInstance		True if the type the event is on is a singleton/static class.
 * @return									Thunk type definition, followed by thunk static field.
 */
std::string GenerateEventThunkSignature(const MethodInfo& eventInfo, bool classHasGlobalSingleInstance)
{
	const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);

	std::stringstream output;
	output << "\t\ttypedef void(B3D_THUNKCALL *" << eventInfo.NativeName << "ThunkDefinition) (";
	
	if (!isStatic && !classHasGlobalSingleInstance)
		output << "MonoObject*, ";

	for (auto I = eventInfo.Parameters.begin(); I != eventInfo.Parameters.end(); ++I)
	{
		TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(I->TypeInformation);

		if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct)
			output << "MonoObject* " << I-> Name << ", ";
		else
			output << GetCppInteropQualifiedTypeName(I->TypeInformation, parameterTypeMappingInformation) << " " << I->Name << ", ";
	}

	output << "MonoException**);" << std::endl;
	output << "\t\tstatic " << eventInfo.NativeName << "ThunkDefinition " << eventInfo.NativeName << "Thunk;" << std::endl;

	return output.str();
}

/** Generates a MonoUtil::String/WstringToMono method call, appropriate to the provided type. Only valid for path and string types. */
static std::string GenerateStringToMonoCall(ExportedClassTypeCategory exportedClassTypeCategory, const std::string& argument)
{
	switch (exportedClassTypeCategory)
	{
		case ExportedClassTypeCategory::Path:
			return "MonoUtil::StringToMono(" + argument + ".ToString())";
		case ExportedClassTypeCategory::String:
			return "MonoUtil::StringToMono(" + argument + ")";
		case ExportedClassTypeCategory::ConstCharString:
			return "MonoUtil::StringToMono(" + argument + ")";
		case ExportedClassTypeCategory::WString:
			return "MonoUtil::WstringToMono(" + argument + ")";
	default:
		assert(false && "Invalid type for this method.");
		return argument;
	}
}

/** Generates a MonoUtil::String/WstringToMono method call, appropriate to the provided type. Only valid for path and string types. */
static std::string GenerateMonoToStringCall(ExportedClassTypeCategory exportedClassTypeCategory, const std::string& argument)
{
	switch (exportedClassTypeCategory)
	{
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::ConstCharString:
		case ExportedClassTypeCategory::Path:
			return "MonoUtil::MonoToString(" + argument + ")";
		case ExportedClassTypeCategory::WString:
			return "MonoUtil::MonoToWstring(" + argument + ")";
	default:
		assert(false && "Invalid type for this method.");
		return argument;
	}
}

/**
 * Returns code that retrieves a script object wrapper from a script object.
 *
 * @param indent					Indent to apply to the generated line of code.
 * @param scriptWrapperType			Script wrapper type.
 * @param scriptWrapperVariableName	Name of the script wrapper variable to store the result in.
 * @param scriptObjectVariableName	Name of the variable containing the Mono object.
 * @param typeInformation			Information about the native type.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @return							Code that retrieves a script object wrapper from a script object, assigning it to a variable named @p scriptWrapperVariableName.
 */
static std::string GenerateScriptObjectToScriptObjectWrapper(const std::string& indent, const std::string& scriptWrapperType, const std::string& scriptWrapperVariableName, const std::string& scriptObjectVariableName, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	const bool isRRef = typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
	const bool isBase = typeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsReferencingBaseClass);

	std::stringstream output;
	if (!isBase || isRRef)
	{
		output << indent << scriptWrapperType << "* " << scriptWrapperVariableName << ";" << std::endl;

		output << indent << scriptWrapperVariableName << " = " << scriptWrapperType << "::GetScriptObjectWrapper(" << scriptObjectVariableName << ");" << std::endl;
	}
	else
	{
		std::string scriptBaseType;
		scriptBaseType = scriptWrapperType + "WrapperBase";

		output << indent << scriptBaseType << "* " << scriptWrapperVariableName << ";" << std::endl;

		output << indent << scriptWrapperVariableName << " = (" << scriptBaseType << "*)" << scriptWrapperType << "::GetScriptObjectWrapper(" << scriptObjectVariableName << ");" << std::endl;
	}

	return output.str();
}

/**
 * Returns code that retrieves a native object from a script object.
 *
 * @param typeInformation				Information about the native type.
 * @param typeMappingInformation		Mapping of the provided type in script.
 * @param name							Name of the field or variable we're converting.
 * @param scriptObjectVariableName		Name of the variable containing the Mono object.
 * @param nativeObjectVariableName		Name of the native object variable to store the result in.
 * @param indent						Indent to apply to the generated line of code.
 * @return								Code that retrieves a native object from a script object, assigning it to a variable named @p nativeObjectVariableName.
 */
static std::string GenerateScriptObjectToNativeObject(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& name, const std::string& scriptObjectVariableName, const std::string& nativeObjectVariableName, const std::string& indent = "\t\t")
{
	std::stringstream output;
	std::string additionalIndent;

	if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::IReflectable)
	{
		output << indent << nativeObjectVariableName << " = ScriptAssemblyManager::Instance().GetReflectableFromManagedObject(" << scriptObjectVariableName << ");\n";
	}
	else
	{
		const bool asResourceReference = typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource && typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
		const std::string scriptObjectWrapperType = TypeLookup::GetScriptWrapperObjectTypeName(typeInformation.GetLastWrappedOrSelfTypeName(), asResourceReference);
		const std::string scriptWrapperVariableName = "scriptObjectWrapper" + name;

		output << GenerateScriptObjectToScriptObjectWrapper(indent, scriptObjectWrapperType, scriptWrapperVariableName, scriptObjectVariableName, typeInformation, typeMappingInformation);
		output << indent << "if(" << scriptWrapperVariableName << " != nullptr)\n";

		output << indent << "\t" << nativeObjectVariableName << " = " << GenerateGetNativeObjectCallLine(typeInformation, typeMappingInformation, scriptWrapperVariableName) << ";\n";
	}

	return output.str();
}

/**
 * Returns code that retrieves a native object from a script object, and writes it into array using 'elementIndex' as the array index.
 *
 * @param arrayElementTypeInformation	Information about the array elements native type.
 * @param typeMappingInformation		Mapping of the provided type in script.
 * @param name							Name of the field or variable we're converting.
 * @param scriptObjectVariableName		Name of the variable containing the Mono object.
 * @param nativeObjectArrayVariableName	Name of the native object array variable to store the result in.
 * @param indent						Indent to apply to the generated line of code.
 * @return								Code that retrieves a native object from a script object, assigning it to an array named @p nativeObjectArrayVariableName using 'elementIndex' as the array index.
 */
static std::string GenerateScriptObjectToNativeObjectAsArrayElement(const VariableTypeInformation& arrayElementTypeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& name, const std::string& scriptObjectVariableName, const std::string& nativeObjectArrayVariableName, const std::string& indent = "\t\t")
{
	std::stringstream output;
	std::string additionalIndent;

	std::string arrayElementPointerType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, typeMappingInformation);
	std::string arrayElementPointerName = "arrayElementPointer" + name;

	output << indent << arrayElementPointerType << " " << arrayElementPointerName << ";\n";

	if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::IReflectable)
	{
		output << indent << arrayElementPointerName << " = ScriptAssemblyManager::Instance().GetReflectableFromManagedObject(" << scriptObjectVariableName << ");\n";
	}
	else
	{
		const bool asResourceReference = typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource && arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef);
		const std::string scriptObjectWrapperType = TypeLookup::GetScriptWrapperObjectTypeName(arrayElementTypeInformation.GetLastWrappedOrSelfTypeName(), asResourceReference);
		const std::string scriptWrapperVariableName = "scriptObjectWrapper" + name;

		output << GenerateScriptObjectToScriptObjectWrapper(indent, scriptObjectWrapperType, scriptWrapperVariableName, scriptObjectVariableName, arrayElementTypeInformation, typeMappingInformation);
		output << indent << "if(" << scriptWrapperVariableName << " != nullptr)\n";
		output << indent << "{\n";
		output << indent << "\t" << arrayElementPointerName << " = " << GenerateGetNativeObjectCallLine(arrayElementTypeInformation, typeMappingInformation, scriptWrapperVariableName) << ";\n";

		additionalIndent = "\t";
	}

	if(!nativeObjectArrayVariableName.empty())
	{
		if(typeMappingInformation.IsClassType())
		{
			// Cast from SPtr to the destination type
			if (arrayElementTypeInformation.TypeCategory == VariableTypeCategory::General)
			{
				if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
				{
					output << indent << additionalIndent << nativeObjectArrayVariableName << "[elementIndex] = " << arrayElementPointerName << ".get();\n";
				}
				else
				{
					output << indent << additionalIndent<< "if(" << arrayElementPointerName << ")\n";
					output << indent << additionalIndent<< "\t" << nativeObjectArrayVariableName << "[elementIndex] = *" << arrayElementPointerName << ";\n";
				}
			}
			else
			{
				if (arrayElementTypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
					errs() << "Error: Invalid type for \"" << name << "\"\n";

				if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					errs() << "Error: Invalid type for \"" << name << "\"\n";

				output << indent << additionalIndent << nativeObjectArrayVariableName << "[elementIndex] = " << arrayElementPointerName << ";\n";
			}
		}
		else
			output << indent << additionalIndent << nativeObjectArrayVariableName << "[elementIndex] = " << arrayElementPointerName << ";\n";

		if(arrayElementTypeInformation.TypeCategory != VariableTypeCategory::IReflectable)
			output << indent << "}\n";
	}

	return output.str();
}

/**
 * Converts a native class type argument into MonoObject. This should be only called on types that are exported to scripting a ExportedClassTypeCategory::Class or ExportedClassTypeCategory::ReflectableClass.
 *
 * @param typeInformation			Information about the native type to convert.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @param outputVariableName		Name of the variable to store the result in.
 * @param scriptType				Interop script type we're doing the conversion for.
 * @param inputVariableName			Name of the variable that's being converted.
 * @param performReferenceCopy		If true, reference copy operation will be performed when assigning the value to output. Required if writing the output as an output parameter to an internal method.
 * @param indent					Optional indent to apply to the generated code.
 * @return							Code that converts a native object to a MonoObject.
 */
static std::string GenerateNativeClassToMonoObject(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& outputVariableName, 
	const std::string& scriptType, const std::string& inputVariableName, bool performReferenceCopy = false, const std::string& indent = "\t\t")
{
	std::stringstream output;

	if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::IReflectable)
	{
		if (performReferenceCopy)
			output << "\t\t" << "MonoUtil::ReferenceCopy(" << outputVariableName << ", ScriptAssemblyManager::Instance().GetManagedObjectFromReflectable(" << inputVariableName << "));\n";
		else
			output << "\t\t" << outputVariableName << " = ScriptAssemblyManager::Instance().GetManagedObjectFromReflectable(" << inputVariableName << ");\n";
	}
	else
	{
		if(performReferenceCopy)
			output << indent << "MonoUtil::ReferenceCopy(" << outputVariableName << ", " << scriptType << "::GetOrCreateScriptObject(" << inputVariableName << "));\n";
		else
			output << indent << outputVariableName << " = " << scriptType << "::GetOrCreateScriptObject(" << inputVariableName << ");\n";
	}

	return output.str();
}

/**
 * Converts a native handle type argument into MonoObject. This should be only called on game or resource object types.
 *
 * @param typeInformation			Information about the native type to convert.
 * @param typeMappingInformation	Mapping of the provided type in script.
 * @param inputVariableName			Name of the variable that's being converted.
 * @param arrayIndexVariable		Optional variable that specifies an array index to lookup in the @p inputVariableName. If non-empty, it's assumed both input and output are arrays.
 * @param scriptVariableName		Name of the temporary script type to store the script interop object in.
 * @param outputVariableName		Name of the variable to store the result in.
 * @param isOutputParameter			True if the output variable is an output parameter to scripting.
 * @param indent					Optional indent to apply to the generated code.
 * @return							Code that converts a native object to a MonoObject.
 */
static std::string GenerateNativeHandleToMonoObject(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, const std::string& inputVariableName, const std::string& arrayIndexVariable, const std::string& scriptVariableName, 
	const std::string& outputVariableName, bool isOutputParameter, const std::string& indent = "\t\t")
{
	// NOTE: scriptVariableName can be automatically deduced from output variable name, but I'm avoding doing that right now to prevent changes to generated code
	//const std::string scriptVariableName = "script" + outputVariableName;
	std::stringstream output;

	const std::string inputVariableAccess = arrayIndexVariable.empty() ? inputVariableName : inputVariableName + "[" + arrayIndexVariable + "]";
	const std::string temporaryScriptObjectVariableName = "temp" + outputVariableName;
	bool isUsingScriptWrapperVariable = true;

	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		if(typeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
		{
			output << indent << "ScriptRRefBase* " << scriptVariableName << ";\n";
			output << indent << scriptVariableName << " = ScriptResourceManager::Instance().GetScriptRRef(" << inputVariableAccess << ");\n";
		}
		else
		{
			isUsingScriptWrapperVariable = false;

			output << indent << "MonoObject* " << temporaryScriptObjectVariableName << " = nullptr;\n";
			output << indent << "if(" << inputVariableAccess << ")\n";
			output << indent << temporaryScriptObjectVariableName << " = ScriptResourceWrapper::GetOrCreateScriptObject(" << inputVariableAccess << ");\n";
		}
	}
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GameObject)
	{
		isUsingScriptWrapperVariable = false;

		output << indent << "MonoObject* " << temporaryScriptObjectVariableName << " = nullptr;\n";
		output << indent << "if(" << inputVariableAccess << ")\n";
		output << indent << temporaryScriptObjectVariableName << " = ScriptGameObject::GetOrCreateScriptObject(" << inputVariableAccess << ");\n";
	}
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Component)
	{
		isUsingScriptWrapperVariable = false;

		output << indent << "MonoObject* " << temporaryScriptObjectVariableName << " = nullptr;\n";
		output << indent << "if(" << inputVariableAccess << ")\n";
		output << indent << "\t" << temporaryScriptObjectVariableName << " = ScriptComponent::GetOrCreateScriptObject(" << inputVariableAccess << ");\n";
	}
	else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::SceneObject)
	{
		isUsingScriptWrapperVariable = false;

		output << indent << "MonoObject* " << temporaryScriptObjectVariableName << " = nullptr;\n";
		output << indent << "if(" << inputVariableAccess << ")\n";
		output << indent << temporaryScriptObjectVariableName << " = ScriptSceneObject::GetOrCreateScriptObject(" << inputVariableAccess << ");\n";
	}
	else
		assert(false && "Unsupported type category");

	if(isUsingScriptWrapperVariable)
		output << indent << "if(" << scriptVariableName << " != nullptr)\n";

	if(arrayIndexVariable.empty())
	{
		if(isOutputParameter)
		{
			if(!isUsingScriptWrapperVariable)
				output << indent << "MonoUtil::ReferenceCopy(" << outputVariableName << ", " << temporaryScriptObjectVariableName << ");\n";
			else
				output << indent << "\tMonoUtil::ReferenceCopy(" << outputVariableName << ", " << scriptVariableName << "->GetScriptObject());\n";

			output << indent << "else\n";
			output << indent << "\t*" << outputVariableName << " = nullptr;\n";
		}
		else
		{
			if(!isUsingScriptWrapperVariable)
				output << indent << outputVariableName << " = " << temporaryScriptObjectVariableName << ";\n";
			else
				output << indent << "\t" << outputVariableName << " = " << scriptVariableName << "->GetScriptObject();\n";

			if(isUsingScriptWrapperVariable)
			{
				output << indent << "else\n";
				output << indent << "\t" << outputVariableName << " = nullptr;\n";
			}
		}
	}
	else
	{
		if(!isUsingScriptWrapperVariable)
			output << indent << outputVariableName << ".Set(" << arrayIndexVariable << ", " << temporaryScriptObjectVariableName << ");\n";
		else
			output << indent << "\t" << outputVariableName << ".Set(" << arrayIndexVariable << ", " << scriptVariableName << "->GetScriptObject());\n";

		if(isUsingScriptWrapperVariable)
		{
			output << indent << "else\n";
			output << indent << "\t" << outputVariableName << ".Set(" << arrayIndexVariable << ", nullptr);\n";
		}
	}

	return output.str();
}

/**
 * Generates code required for passing an argument to an interop method.
 *
 * @param parameterName				Name of the parameter variable.
 * @param parameterInformation		Information about the parameter the argument is being passed to.
 * @param isLast					True if this is the last argument passed to the method/function.
 * @param returnValue				True if the argument is in fact a return value.
 * @param preCallActions			Actions to execute before the method/function call.
 * @param postCallActions			Actions to execute after the method/function call.
 * @return							Name of the argument.
 */
static std::string GenerateMethodBodyBlockForArgument(const std::string& parameterName, const VariableBase& parameterInformation, bool isLast, bool returnValue, std::stringstream& preCallActions, std::stringstream& postCallActions)
{
	TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

	// Handle AsyncOp types
	if(parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		const VariableTypeInformation& asyncOpUnderlyingTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		if (!parameterInformation.TypeInformation.IsOutputParameter(parameterTypeMappingInformation) && !returnValue)
		{
			outs() << "Error: AsyncOp type not supported as input parameter. \n";
			return "";
		}

		if (parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::ReflectableClass && parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::Class && parameterTypeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::Resource)
		{
			outs() << "Error: Type not supported as an AsyncOp return value. \n";
			return "";
		}

		std::string argumentType;
		std::string argumentName;
		if (!asyncOpUnderlyingTypeInformation.IsArrayOrVector())
		{
			argumentName = "tmp" + parameterName;
			const std::string asyncOpType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			argumentType = GetCppNativeQualifiedTypeName(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation);

			preCallActions << "\t\t" << asyncOpType << " " << argumentName << ";\n";
		}
		else
		{
			argumentType = GetCppNativeQualifiedTypeName(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation, false);
			argumentName = "vec" + parameterName;

			preCallActions << "\t\t" << argumentType << " " << argumentName;
			if (asyncOpUnderlyingTypeInformation.TypeCategory == VariableTypeCategory::Array)
				preCallActions << "[" << asyncOpUnderlyingTypeInformation.ArraySize << "]";
			preCallActions << ";\n";
		}

		std::string monoType;
		if(asyncOpUnderlyingTypeInformation.GetLastWrappedOrSelfTypeName() != "Any")
		{
			std::string scriptType = TypeLookup::GetScriptWrapperObjectTypeName(asyncOpUnderlyingTypeInformation.GetLastWrappedOrSelfTypeName(),
				parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource && asyncOpUnderlyingTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));

			monoType = scriptType + "::GetMetaData()->ScriptClass";

			postCallActions << "\t\tauto fnConvertCallback = [](const Any& returnValue)\n";
			postCallActions << "\t\t{\n";
			postCallActions << "\t\t\t" << argumentType << " nativeObject = AnyCast<" << argumentType << ">(returnValue);\n";
			postCallActions << "\t\t\tMonoObject* scriptObject;\n";

			if (!asyncOpUnderlyingTypeInformation.IsArrayOrVector())
			{
				if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::ReflectableClass || parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Class)
					postCallActions << GenerateNativeClassToMonoObject(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation, "scriptObject", scriptType, "nativeObject", false, "\t\t\t");
				else // Resource
				{
					postCallActions << GenerateNativeHandleToMonoObject(asyncOpUnderlyingTypeInformation, parameterTypeMappingInformation, "nativeObject", "", "scriptWrapperObject", "scriptObject", false, "\t\t\t");
				}
			}
			else
			{
				const std::string arrayName = "scriptArray";

				postCallActions << "\t\t\tint elementCount = ";
				if (asyncOpUnderlyingTypeInformation.IsArrayOrVector(false))
					postCallActions << "(int)" << argumentName << ".size()";
				else
					postCallActions << asyncOpUnderlyingTypeInformation.ArraySize;
				postCallActions << ";\n";

				postCallActions << "\t\t\tScriptArray " << arrayName;
				postCallActions << " = " << "ScriptArray::Create<" << scriptType << ">(arraySize);" << std::endl;
				postCallActions << "\t\t\tfor(int elementIndex = 0; elementIndex < elementCount; elementIndex++)" << std::endl;
				postCallActions << "\t\t\t{" << std::endl;

				const VariableTypeInformation& arrayElementTypeInformation = asyncOpUnderlyingTypeInformation.AssertGetUnderlyingType();

				switch (parameterTypeMappingInformation.TypeCategory)
				{
				case ExportedClassTypeCategory::ReflectableClass:
				case ExportedClassTypeCategory::Class:
				{
					const std::string arrayElementName = "arrayElement" + parameterName;

					const std::string arrayElementPtrType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
					const std::string arrayElementPtrName = "arrayElementPointer" + parameterName;

					postCallActions << "\t\t\t\t" << arrayElementPtrType << " " << arrayElementPtrName;
					if (arrayElementTypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
					{
						postCallActions << " = B3DMakeShared<" << parameterInformation.TypeInformation.TypeName << ">();\n";

						if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
						{
							postCallActions << "\t\t\t\tif(nativeObject[elementIndex])\n";
							postCallActions << "\t\t\t\t\t*" << arrayElementPtrName << " = *";
						}
						else
						{
							postCallActions << "\t\t\t\t*" << arrayElementPtrName << " = ";
						}

						postCallActions << "nativeObject[elementIndex];\n";
					}
					else
					{
						postCallActions << " = nativeObject[elementIndex];\n";
					}

					postCallActions << "\t\t\t\tMonoObject* " << arrayElementName << ";\n";
					postCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, arrayElementName, scriptType, arrayElementPtrName, false, "\t\t\t\t");

					postCallActions << "\t\t\t\t" << arrayName << ".Set(elementIndex, " << arrayElementName << ");" << std::endl;
					break;
				}
				case ExportedClassTypeCategory::Resource:
				{
					postCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, "nativeObject", "elementIndex", "scriptWrapperObject", arrayName, false, "\t\t\t\t");
				}
				break;
				default:
					outs() << "Error: Type not supported as an AsyncOp return value. \n";
					break;
				}

				postCallActions << "\t\t\t}" << std::endl;
				postCallActions << "\t\t\tscriptObject = " << arrayName << ".GetInternal();" << std::endl;
			}

			postCallActions << "\t\t\treturn scriptObject;\n";
			postCallActions << "\t\t};\n";
			postCallActions << "\n;";
		}
		else
			postCallActions << "\t\tauto fnConvertCallback = nullptr;\n";

		if (returnValue)
			postCallActions << "\t\t" << parameterName << " = " << "ScriptAsyncOpBase::Create(" << argumentName << ", fnConvertCallback, " << monoType << ");\n";
		else
			postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << "ScriptAsyncOpBase::Create(" << argumentName << ", fnConvertCallback, " << monoType << "));\n";

		return argumentName;
	}

	// Converts a native struct into an interop struct
	auto fnGenerateConvertToInteropStructPostCallActions = [&postCallActions](const std::string& inputVariableName, const std::string& outputVariableName, const std::string& inputType)
	{
		const std::string scriptType = TypeLookup::GetScriptWrapperObjectTypeName(inputType);

		postCallActions << "\t\t" << GetStructInteropTypeName(inputType) << " interop" << outputVariableName << ";\n";
		postCallActions << "\t\tinterop" << outputVariableName << " = " << scriptType << "::ToInterop(" << inputVariableName << ");\n";

		postCallActions << "\t\tMonoUtil::ValueCopy(" << outputVariableName << ", ";
		postCallActions << "&interop" << outputVariableName << ", ";
		postCallActions << scriptType << "::GetMetaData()->ScriptClass->GetInternalClass());\n";
	};

	const std::string parameterTypeName = parameterInformation.TypeInformation.GetLastWrappedOrSelfTypeName();
	const bool isOutputParameter = parameterInformation.TypeInformation.IsOutputParameter(parameterTypeMappingInformation);

	// Handle non-array types
	if (!parameterInformation.TypeInformation.IsArrayOrVector())
	{
		const bool isFlags = parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags;
		const bool isPlainType =
			parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Primitive ||
			parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::Flags ||
			(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));
		const bool isClassType = parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Class || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ReflectableClass;

		// Primitive and non-complex structs can be passed as-is, except for return values, in which case we need to box them.
		// Flags need to be converted to their underlying enum type if they are an output parameter or return value.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired = returnValue || (isOutputParameter && !isPlainType) || (!isPlainType && !isFlags);

		// Temporary is needed for any type that cannot be passed directly between native and interop
		std::string argumentName;
		if (isTemporaryRequired)
		{
			argumentName = "tmp" + parameterName;

			const std::string fullTypeName = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
			preCallActions << "\t\t" << fullTypeName << " " << argumentName;

			if (isClassType && (returnValue || isOutputParameter) && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
				preCallActions << " = B3DMakeShared<" << parameterTypeName << ">()"; // We'll be copying by value rather than just assigning the pointer, so initialize the destination
			else if(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::MonoObject || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::MonoReflectionType || parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::ConstCharString)
				preCallActions << " = nullptr";

			preCallActions << ";\n";
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::Enum:
		case ExportedClassTypeCategory::Struct:
		{
			if (returnValue)
			{
				if (parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
				{
					if (parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
						fnGenerateConvertToInteropStructPostCallActions(argumentName, parameterName, parameterTypeName);
					else
						postCallActions << "\t\t*" << parameterName << " = " << argumentName << ";" << std::endl;
				}
				else if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
					postCallActions << "\t\t" << parameterName << " = (" << parameterTypeName << ")(uint32_t)" << argumentName << ";" << std::endl;
				else
					postCallActions << "\t\t" << parameterName << " = " << argumentName << ";" << std::endl;
			}
			else if (isOutputParameter)
			{
				if (parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					fnGenerateConvertToInteropStructPostCallActions(argumentName, parameterName, parameterTypeName);
				}
				else if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
				{
					postCallActions << "\t\t*" << parameterName << " = (" << parameterTypeName << ")(uint32_t)" << argumentName << ";" << std::endl;
				}
				else
					argumentName = parameterName;
			}
			else
			{
				if (parameterTypeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Struct && parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					std::string scriptType = TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName);
					preCallActions << "\t\t" << argumentName << " = " << scriptType << "::FromInterop(*" << parameterName << ");" << std::endl;
				}
				else
					argumentName = parameterName;
			}
		}

			break;
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::Path:
		{
			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ";\n";
			else if (isOutputParameter)
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ",  (MonoObject*)" << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ");\n";
			else
				preCallActions << "\t\t" << argumentName << " = " << GenerateMonoToStringCall(parameterTypeMappingInformation.TypeCategory, parameterName) << ";\n";
		}
		break;
		case ExportedClassTypeCategory::ConstCharString:
		{
			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ";\n";
			else if (isOutputParameter)
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ",  (MonoObject*)" << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, argumentName) << ");\n";
			else
			{
				errs() << "Error: const char* type not supported as input or array element. Ignoring. \n";
			}
		}
		break;
		case ExportedClassTypeCategory::MonoObject:
		case ExportedClassTypeCategory::MonoReflectionType:
		{
			if(returnValue)
				postCallActions << "\t\t" << parameterName << " = " << argumentName << ";" << std::endl;
			else if(isOutputParameter)
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", " << argumentName << ");" << std::endl;
			else
				preCallActions << "\t\t" << argumentName << " = " << parameterName << ";\n";
		}
		break;
		case ExportedClassTypeCategory::GUIElement:
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		case ExportedClassTypeCategory::IReflectable:
		{
			const std::string scriptObjectWrapperType = parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::IReflectable ? TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName) : "";

			if (returnValue)
				postCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, parameterName, scriptObjectWrapperType, argumentName);
			else if (isOutputParameter)
				postCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, parameterName, scriptObjectWrapperType, argumentName, true);
			else
				preCallActions << GenerateScriptObjectToNativeObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, parameterName, parameterName, argumentName);
		}
			break;
		default: // Some resource or game object type
		{
			if (returnValue || isOutputParameter)
				postCallActions << GenerateNativeHandleToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, argumentName, "", "script" + parameterName, parameterName, isOutputParameter);
			else
				preCallActions << GenerateScriptObjectToNativeObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, parameterName, parameterName, argumentName);
		}
		break;
		}

		return argumentName;
	}

	// Handle array types
	assert(parameterInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		std::string arrayEntryTypeName;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			arrayEntryTypeName = parameterTypeName;
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			arrayEntryTypeName = "MonoObject*";
			break;
		case ::ExportedClassTypeCategory::MonoReflectionType:
			arrayEntryTypeName = "MonoReflectionType*";
			break;
		default: // Some object or struct type
			arrayEntryTypeName = TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName, arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			break;
		}

		const std::string arrayArgumentType = GetCppNativeQualifiedTypeName(parameterInformation.TypeInformation, parameterTypeMappingInformation);
		const std::string arrayArgumentName = "nativeArray" + parameterName;

		preCallActions << "\t\t" << arrayArgumentType << " " << arrayArgumentName;
		if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Array)
			preCallActions << "[" << parameterInformation.TypeInformation.ArraySize << "]";
		preCallActions << ";\n";

		if (!isOutputParameter && !returnValue)
		{
			const std::string scriptArrayName = "scriptArray" + parameterName;

			preCallActions << "\t\tif(" << parameterName << " != nullptr)\n";
			preCallActions << "\t\t{\n";

			preCallActions << "\t\t\tScriptArray " << scriptArrayName << "(" << parameterName << ");\n";

			if (parameterInformation.TypeInformation.IsArrayOrVector(false))
				preCallActions << "\t\t\t" << arrayArgumentName << ".resize(" << scriptArrayName << ".Size());\n";

			preCallActions << "\t\t\tfor(int elementIndex = 0; elementIndex < (int)" << scriptArrayName << ".Size(); elementIndex++)\n";
			preCallActions << "\t\t\t{\n";

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ExportedClassTypeCategory::Primitive:
			case ExportedClassTypeCategory::String:
			case ExportedClassTypeCategory::WString:
			case ExportedClassTypeCategory::Path:
			
				preCallActions << "\t\t\t\t" << arrayArgumentName << "[elementIndex] = " << scriptArrayName << ".Get<" << arrayEntryTypeName << ">(elementIndex);" << std::endl;
				break;
			case ExportedClassTypeCategory::ConstCharString:
				errs() << "Error: const char* type not supported as an array element or input. Ignoring. \n";
				break;
			case ExportedClassTypeCategory::MonoObject:
				errs() << "Error: Array of MonoObject types not supported as input. Ignoring. \n";
				break;
			case ExportedClassTypeCategory::MonoReflectionType:
				errs() << "Error: Array of MonoReflectionType types not supported as input. Ignoring. \n";
				break;
			case ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preCallActions << "\t\t\t\t" << arrayArgumentName << "[elementIndex] = (" << arrayEntryTypeName << ")" << scriptArrayName << ".Get<" << enumType << ">(elementIndex);" << std::endl;
				break;
			}
			case ExportedClassTypeCategory::Struct:

				preCallActions << "\t\t\t\t" << arrayArgumentName << "[elementIndex] = ";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					preCallActions << arrayEntryTypeName << "::FromInterop(";
					preCallActions << scriptArrayName << ".Get<" << GetStructInteropTypeName(parameterTypeName) << ">(elementIndex)";
					preCallActions << ")";
				}
				else
					preCallActions << scriptArrayName << ".Get<" << parameterTypeName << ">(elementIndex)";

				preCallActions << ";\n";

				break;
			default: // Some object type
			{
				preCallActions << GenerateScriptObjectToNativeObjectAsArrayElement(arrayElementTypeInformation, parameterTypeMappingInformation, parameterName, scriptArrayName + ".Get<MonoObject*>(elementIndex)", arrayArgumentName, "\t\t\t\t");
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
			const std::string scriptArrayName = "scriptArray" + parameterName;

			postCallActions << "\t\tint elementCount" << parameterName << " = ";
			if (parameterInformation.TypeInformation.IsArrayOrVector(false))
				postCallActions << "(int)" << arrayArgumentName << ".size()";
			else
				postCallActions << parameterInformation.TypeInformation.ArraySize;
			postCallActions << ";\n";

			if(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::MonoReflectionType)
			{
				postCallActions << "\t\tScriptArray " << scriptArrayName << " = ScriptArray(*ScriptAssemblyManager::Instance().GetBuiltinClasses().SystemTypeClass, elementCount" << parameterName << ");\n";
			}
			else
			{
				postCallActions << "\t\tScriptArray " << scriptArrayName;
				if(parameterTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum)
					postCallActions << " = " << "ScriptArray::Create<u32>(elementCount" << parameterName << ");" << std::endl; // TODO - Handle this more gracefully
				else
					postCallActions << " = " << "ScriptArray::Create<" << arrayEntryTypeName << ">(elementCount" << parameterName << ");" << std::endl;

				postCallActions << "\t\tfor(int elementIndex = 0; elementIndex < elementCount" << parameterName << "; elementIndex++)" << std::endl;
				postCallActions << "\t\t{" << std::endl;
			}

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ExportedClassTypeCategory::Primitive:
			case ExportedClassTypeCategory::String:
			case ExportedClassTypeCategory::WString:
			case ExportedClassTypeCategory::Path:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, " << arrayArgumentName << "[elementIndex]);" << std::endl;
				break;
			case ExportedClassTypeCategory::ConstCharString:
				errs() << "Error: const char* type not supported as an array element. Ignoring. \n";// TODO - Can be easily added by adding ScriptArray specialization
				break;
			case ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::Flags)
					postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, (" << enumType << ")(uint32_t)" << arrayArgumentName << "[elementIndex]);" << std::endl;
				else
					postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, (" << enumType << ")" << arrayArgumentName << "[elementIndex]);" << std::endl;
				break;
			}
			case ExportedClassTypeCategory::Struct:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, ";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					postCallActions << arrayEntryTypeName << "::ToInterop(";

				postCallActions << arrayArgumentName << "[elementIndex]";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					postCallActions << ")";

				postCallActions << ");\n";

				break;
			case ExportedClassTypeCategory::MonoObject:
			case ExportedClassTypeCategory::MonoReflectionType:
				postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, " << arrayArgumentName << "[elementIndex]);" << std::endl;
				break;
			case ExportedClassTypeCategory::Class:
			case ExportedClassTypeCategory::ReflectableClass:
			case ExportedClassTypeCategory::GUIElement:
			case ExportedClassTypeCategory::IReflectable:
			{
				const std::string arrayElementName = "arrayElement" + parameterName;

				const std::string arrayElementPointerType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				const std::string arrayElementPointerName = "arrayElementPointer" + parameterName;

				postCallActions << "\t\t\t" << arrayElementPointerType << " " << arrayElementPointerName;
				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
				{
					if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
					{
						errs() << "Error: Cannot pass Shared<T> by pointer.";
					}

					postCallActions << " = " << arrayArgumentName << "[elementIndex];\n";
				}
				else
				{
					if(arrayElementTypeInformation.TypeCategory != VariableTypeCategory::General)
					{
						errs() << "Error: Class passed as an invalid type: " << (uint32_t)arrayElementTypeInformation.TypeCategory;
					}

					postCallActions << " = B3DMakeShared<" << arrayElementTypeInformation.GetLastWrappedOrSelfTypeName() << ">();\n";

					if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						postCallActions << "\t\t\tif(" << arrayArgumentName << "[i])\n";
						postCallActions << "\t\t\t\t*" << arrayElementPointerName << " = *";
					}
					else
					{
						postCallActions << "\t\t\t*" << arrayElementPointerName << " = ";
					}

					postCallActions << arrayArgumentName << "[elementIndex];\n";
				}

				postCallActions << "\t\t\tMonoObject* " << arrayElementName << ";\n";

				postCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, arrayElementName, arrayEntryTypeName, arrayElementPointerName, false, "\t\t\t");

				postCallActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, " << arrayElementName << ");" << std::endl;
				break;
			}
			default: // Some resource or game object type
			{
				postCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, arrayArgumentName, "elementIndex", "scriptObjectWrapper" + parameterName, scriptArrayName, false, "\t\t\t");
			}
			break;
			}

			postCallActions << "\t\t}" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << parameterName << " = " << scriptArrayName << ".GetInternal();" << std::endl;
			else
				postCallActions << "\t\tMonoUtil::ReferenceCopy(" << parameterName << ", (MonoObject*)" << scriptArrayName << ".GetInternal());" << std::endl;
		}

		return arrayArgumentName;
	}
}

/**
 * Generates code that converts a struct field from or to interop.
 *
 * @param name					Name of the field that's being converted.
 * @param fieldInformation		Information about the field being converted.
 * @param toInterop				True if converting native to interop, false if converting from interop to native.
 * @param preActions			Stream that can be used for appending code that will execute before the field assignment happens.
 * @return						Name of the variable containing either the native or interop representation of the field.
 */
static std::string GenerateFieldConvertBlock(const std::string& name, const VariableBase& fieldInformation, bool toInterop, std::stringstream& preActions)
{
	TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

	if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		outs() << "Error: AsyncOp type not supported as a struct field. \n";
		return "";
	}

	const std::string& fieldTypeName = fieldInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	// Handle non-array types
	if (!fieldInformation.TypeInformation.IsArrayOrVector())
	{
		std::string argumentVariableName;

		// Primitive, enum and non-complex structs can be passed as-is.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired = parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Primitive && parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum &&
			(parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Struct || fieldInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed));

		if(isTemporaryRequired)
		{
			argumentVariableName = "tmp" + name;
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::Enum:
			argumentVariableName = "value." + name;
			break;
		case ExportedClassTypeCategory::Struct:
			if(fieldInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
			{
				std::string interopType = GetStructInteropTypeName(fieldTypeName);
				std::string scriptWrapperObjectType = TypeLookup::GetScriptWrapperObjectTypeName(fieldTypeName);

				if(toInterop)
				{
					preActions << "\t\t" << interopType << " " << argumentVariableName << ";\n";
					preActions << "\t\t" << argumentVariableName << " = " << scriptWrapperObjectType << "::ToInterop(value." << name << ");\n";
				}
				else
				{
					preActions << "\t\t" << fieldTypeName << " " << argumentVariableName << ";\n";
					preActions << "\t\t" << argumentVariableName << " = " << scriptWrapperObjectType << "::FromInterop(value." << name << ");\n";
				}
			}
			else
				argumentVariableName = "value." + name;
			break;
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::Path:
		{
			if(toInterop)
			{
				preActions << "\t\tMonoString* " << argumentVariableName << ";\n";
				preActions << "\t\t" << argumentVariableName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, "value." + name) << ";\n";
			}
			else
			{
				preActions << "\t\tString " << argumentVariableName << ";\n";
				preActions << "\t\t" << argumentVariableName << " = " << GenerateMonoToStringCall(parameterTypeMappingInformation.TypeCategory, "value." + name) << ";\n";
			}
		}
		break;
		case ExportedClassTypeCategory::ConstCharString:
		{
			if(toInterop)
			{
				preActions << "\t\tMonoString* " << argumentVariableName << ";\n";
				preActions << "\t\t" << argumentVariableName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, "value." + name) << ";\n";
			}
			else
			{
				// Not supported, caller should emit a warning
			}
		}
		break;
		case ExportedClassTypeCategory::MonoObject:
		{
			preActions << "\t\tMonoObject* " << argumentVariableName << ";\n";
			preActions << "\t\t" << argumentVariableName << " = " << name << ";\n";
		}
		break;
		case ExportedClassTypeCategory::MonoReflectionType:
		{
			preActions << "\t\tMonoReflectionType* " << argumentVariableName << ";\n";
			preActions << "\t\t" << argumentVariableName << " = " << name << ";\n";
		}
		break;
		case ExportedClassTypeCategory::GUIElement:
		{
			if(!toInterop)
			{
				if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
				{
					const std::string argumentType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);

					preActions << "\t\t" << argumentType << " " << argumentVariableName << ";\n";
					preActions << GenerateScriptObjectToNativeObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, name, "value." + name, argumentVariableName);
				}
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		case ExportedClassTypeCategory::IReflectable:
		{
			const std::string scriptWrapperObjectType = parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::IReflectable ? TypeLookup::GetScriptWrapperObjectTypeName(fieldTypeName) : "";

			if(toInterop)
			{
				preActions << "\t\tMonoObject* " << argumentVariableName << ";\n";

				if(fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::SharedPointer)
				{
					if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsReference))
					{
						errs() << "Error: Invalid struct member type for \"" << name << "\". Cannot pass Shared<T> by pointer.\n";
					}

					preActions << GenerateNativeClassToMonoObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, argumentVariableName, scriptWrapperObjectType, "value." + name);
				}
				else
				{
					if(fieldInformation.TypeInformation.TypeCategory != VariableTypeCategory::General)
					{
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";
					}
					
					const std::string argumentType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
					preActions << "\t\t" << argumentType << " " << argumentVariableName << "copy;\n";

					// Note: Assuming a copy constructor exists
					if (fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						preActions << "\t\tif(value." << name << " != nullptr)\n";
						preActions << "\t\t\t" << argumentVariableName << "copy = B3DMakeShared<" << fieldTypeName << ">(*value." << name << ");\n";
					}
					else
						preActions << "\t\t" << argumentVariableName << "copy = B3DMakeShared<" << fieldTypeName << ">(value." << name << ");\n";

					preActions << GenerateNativeClassToMonoObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, argumentVariableName, scriptWrapperObjectType, argumentVariableName + "copy");
				}
			}
			else
			{
				const std::string argumentType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);

				preActions << "\t\t" << argumentType << " " << argumentVariableName << ";" << std::endl;
				preActions << GenerateScriptObjectToNativeObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, name, "value." + name, argumentVariableName);

				// Cast to the source type from SPtr
				if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::General)
				{
					if(fieldInformation.TypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						argumentVariableName = argumentVariableName + ".get()";
					}
					else
					{
						preActions << "\t\tif(" << argumentVariableName << " != nullptr)" << std::endl;
						argumentVariableName = "*" + argumentVariableName;
					}
				}
				else if(fieldInformation.TypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
					errs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		default: // Some resource or game object type
		{

			if(toInterop)
			{
				std::string argName = "value." + name;

				preActions << "\t\tMonoObject* " << argumentVariableName << ";\n";

				const std::string scriptWrapperObjectVariableName = "scriptWrapperObject" + name;
				preActions << GenerateNativeHandleToMonoObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, argName, "", scriptWrapperObjectVariableName, argumentVariableName, false);
			}
			else
			{
				const std::string argumentType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);

				preActions << "\t\t" << argumentType << " " << argumentVariableName << ";\n";
				preActions << GenerateScriptObjectToNativeObject(fieldInformation.TypeInformation, parameterTypeMappingInformation, name, "value." + name, argumentVariableName);
			}

			const VariableTypeInformation& underlyingType = fieldInformation.TypeInformation;
			if(underlyingType.TypeCategory != VariableTypeCategory::GameObjectHandle && underlyingType.TypeCategory != VariableTypeCategory::ResourceHandle)
				outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
		}
		break;
		}

		return argumentVariableName;
	}

	// Handle array types
	assert(fieldInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = fieldInformation.TypeInformation.AssertGetUnderlyingType();

		std::string entryType;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ::ExportedClassTypeCategory::Primitive:
		case ::ExportedClassTypeCategory::String:
		case ::ExportedClassTypeCategory::WString:
		case ::ExportedClassTypeCategory::ConstCharString:
		case ::ExportedClassTypeCategory::Path:
		case ::ExportedClassTypeCategory::Enum:
			entryType = arrayElementTypeInformation.GetLastWrappedOrSelfTypeName();
			break;
		case ::ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		case ::ExportedClassTypeCategory::MonoReflectionType:
			entryType = "MonoReflectionType*";
			break;
		default: // Some object or struct type
			entryType = TypeLookup::GetScriptWrapperObjectTypeName(fieldTypeName, arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			break;
		}

		const std::string argumentType = GetCppNativeQualifiedTypeName(fieldInformation.TypeInformation, parameterTypeMappingInformation);
		const std::string argumentVariableName = "vec" + name;

		if (!toInterop)
		{
			const std::string scriptArrayName = "scriptArray" + name;
			preActions << "\t\t" << argumentType << " " << argumentVariableName;
			if (fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::Array)
				preActions << "[" << fieldInformation.TypeInformation.ArraySize << "]";
			preActions << ";" << std::endl;

			preActions << "\t\tif(value." << name << " != nullptr)\n";
			preActions << "\t\t{\n";
			preActions << "\t\t\tScriptArray " << scriptArrayName << "(value." << name << ");\n";

			if (fieldInformation.TypeInformation.IsArrayOrVector(false))
				preActions << "\t\t\t" << argumentVariableName << ".resize(" << scriptArrayName << ".Size());\n";

			preActions << "\t\t\tfor(int elementIndex = 0; elementIndex < (int)" << scriptArrayName << ".Size(); elementIndex++)" << std::endl;
			preActions << "\t\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t\t" << argumentVariableName << "[elementIndex] = " << scriptArrayName << ".Get<" << entryType << ">(elementIndex);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::MonoObject:
				errs() << "Error: Array of MonoObject types not supported as input. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::MonoReflectionType:
				errs() << "Error: Array of MonoReflectionType types not supported as input. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::ConstCharString:
				errs() << "Error: const char* type not supported as input or array element. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t\t" << argumentVariableName << "[elementIndex] = (" << entryType << ")" << scriptArrayName << ".get<" << enumType << ">(elementIndex);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t\t" << argumentVariableName << "[elementIndex] = ";

				if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					preActions << entryType << "::FromInterop(";
					preActions << scriptArrayName << ".Get<" << GetStructInteropTypeName(fieldTypeName) << ">(elementIndex)";
					preActions << ")";
				}
				else
					preActions << scriptArrayName << ".Get<" << fieldTypeName << ">(elementIndex)";

				preActions << ";\n";
				break;
			default: // Some object type
			{
				preActions << GenerateScriptObjectToNativeObjectAsArrayElement(arrayElementTypeInformation, parameterTypeMappingInformation, name, scriptArrayName + ".Get<MonoObject*>(elementIndex)", argumentVariableName, "\t\t\t\t");
			}
			break;
			}

			preActions << "\t\t\t}" << std::endl;
			preActions << "\t\t}\n";
		}
		else
		{
			preActions << "\t\tint elementCount" << name << " = ";
			if (fieldInformation.TypeInformation.IsArrayOrVector(false))
				preActions << "(int)value." << name << ".size()";
			else
				preActions << fieldInformation.TypeInformation.ArraySize;
			preActions << ";\n";

			preActions << "\t\tMonoArray* " << argumentVariableName << ";" << std::endl;

			const std::string scriptArrayName = "scriptArray" + name;
			preActions << "\t\tScriptArray " << scriptArrayName;
			preActions << " = " << "ScriptArray::Create<" << entryType << ">(elementCount" << name << ");" << std::endl;
			preActions << "\t\tfor(int elementIndex = 0; elementIndex < elementCount" << name << "; elementIndex++)" << std::endl;
			preActions << "\t\t{" << std::endl;

			switch (parameterTypeMappingInformation.TypeCategory)
			{
			case ::ExportedClassTypeCategory::Primitive:
			case ::ExportedClassTypeCategory::String:
			case ::ExportedClassTypeCategory::WString:
			case ::ExportedClassTypeCategory::Path:
				preActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, value." << name << "[elementIndex]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::Enum:
			{
				std::string enumType;
				ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

				preActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, (" << enumType << ")value." << name << "[elementIndex]);" << std::endl;
				break;
			}
			case ::ExportedClassTypeCategory::Struct:
				preActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, ";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					preActions << entryType << "::ToInterop(";

				preActions << "value." << name << "[elementIndex]";

				if(arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
					preActions << ")";

				preActions << ");\n";
				break;
			case ::ExportedClassTypeCategory::MonoObject:
			case ::ExportedClassTypeCategory::MonoReflectionType:
				preActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, value." << name << "[elementIndex]);" << std::endl;
				break;
			case ::ExportedClassTypeCategory::ConstCharString:
				errs() << "Error: const char* type not supported as array element. Ignoring. \n";
				break;
			case ::ExportedClassTypeCategory::Class:
			case ::ExportedClassTypeCategory::ReflectableClass:
			case ::ExportedClassTypeCategory::IReflectable:
			{
				std::string arrayElementVariableName = "arrayElement" + name;

				std::string arrayElementPointerType = GetCppNativeQualifiedTypeName(arrayElementTypeInformation, parameterTypeMappingInformation);
				std::string arrayElementPointerVariableName = "arrayElementPointer" + name;

				preActions << "\t\t\t" << arrayElementPointerType << " " << arrayElementPointerVariableName;
				if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::General)
				{
					preActions << " = B3DMakeShared<" << fieldTypeName << ">();\n";

					if (arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
					{
						preActions << "\t\t\tif(value." << name << "[elementIndex])\n";
						preActions << "\t\t\t\t*" << arrayElementPointerVariableName << " = *";
					}
					else
					{
						preActions << "\t\t\t*" << arrayElementPointerVariableName << " = ";
					}

					preActions << "value." << name << "[elementIndex];\n";
				}
				else
				{
					if (arrayElementTypeInformation.TypeCategory != VariableTypeCategory::SharedPointer)
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

					if(arrayElementTypeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
						errs() << "Error: Invalid struct member type for \"" << name << "\"\n";

					preActions << " = value." << name << "[elementIndex];\n";
				}

				preActions << "\t\t\tMonoObject* " << arrayElementVariableName << ";\n";
				preActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, arrayElementVariableName, entryType, arrayElementPointerVariableName, false, "\t\t\t");

				preActions << "\t\t\t" << scriptArrayName << ".Set(elementIndex, " << arrayElementVariableName << ");" << std::endl;
			}
			break;
			case ::ExportedClassTypeCategory::GUIElement:
				// Unsupported as output
				break;
			default: // Some resource or game object type
			{
				preActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, "value." + name, "elementIndex", "scriptObjectWrapper" + name, scriptArrayName, false, "\t\t\t");
			}
			break;
			}

			preActions << "\t\t}" << std::endl;
			preActions << "\t\t" << argumentVariableName << " = " << scriptArrayName << ".GetInternal();" << std::endl;
		}

		return argumentVariableName;
	}
}

/**
 * Generates code required for passing an argument from event callback to an event thunk.
 *
 * @param name						Name of the argument variable received in the event callback.
 * @param parameterInformation		Information about the parameter the argument is being passed to.
 * @param preCallActions			Actions to execute before the thunk call.
 * @return							Name of the argument to pass to the thunk.
 */
static std::string GenerateEventCallbackBodyBlockForArgument(const std::string& name, const VariableBase& parameterInformation, std::stringstream& preCallActions)
{
	const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
	const std::string& parameterTypeName = parameterInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	if (parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::AsyncOp)
	{
		errs() << "Error: AsyncOp type not supported as an event callback parameter. \n";
		return "";
	}

	// Handle non-array types
	if (!parameterInformation.TypeInformation.IsArrayOrVector())
	{
		std::string argName;

		// Primitives and non-flags enums can be passed as-is.
		// All other types need conversion to the corresponding Mono type.
		const bool isTemporaryRequired =
			parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Primitive &&
			(parameterTypeMappingInformation.TypeCategory != ExportedClassTypeCategory::Enum || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags);

		if(isTemporaryRequired)
		{
			argName = "tmp" + name;
		}

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
			argName = name;
			break;
		case ExportedClassTypeCategory::Enum:
			if(parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
			{
				preCallActions << "\t\t" << parameterTypeName << argName << ";" << std::endl;
				preCallActions << "\t\t" << argName << " = (" << parameterTypeName << ")(uint32_t)" << name << ";" << std::endl;
			}
			else
				argName = name;
			break;
		case ExportedClassTypeCategory::Struct:
			{
				const std::string scriptType = TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName);
				preCallActions << "\t\tMonoObject* " << argName << ";\n";

				if(parameterInformation.TypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				{
					const std::string interopName = "interop" + name;
					const std::string interopType = GetStructInteropTypeName(parameterTypeName);
					
					preCallActions << "\t\t" << interopType << " " << interopName << ";" << std::endl;
					preCallActions << "\t\t" << interopName << " = " << scriptType << "::ToInterop(" << name << ");" << std::endl;
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << interopName << ");\n";
				}
				else
					preCallActions << "\t\t" << argName << " = " << scriptType << "::Box(" << name << ");\n";
			}

			break;
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::ConstCharString:
		case ExportedClassTypeCategory::Path:
		{
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = " << GenerateStringToMonoCall(parameterTypeMappingInformation.TypeCategory, name) << ";\n";
		}
		break;
		case ExportedClassTypeCategory::MonoObject:
		{
			preCallActions << "\t\tMonoObject* " << argName << " = " << name << ";\n";
		}
		break;
		case ExportedClassTypeCategory::MonoReflectionType:
		{
			preCallActions << "\t\tMonoReflectionType* " << argName << " = " << name << ";\n";
		}
		break;
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		case ExportedClassTypeCategory::IReflectable:
		{
			const std::string scriptType = TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName);

			preCallActions << "\t\tMonoObject* " << argName << ";\n";
			preCallActions << GenerateNativeClassToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, argName, scriptType, name);
		}
			break;
		default: // Some resource or game object type
		{
			preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
			preCallActions << GenerateNativeHandleToMonoObject(parameterInformation.TypeInformation, parameterTypeMappingInformation, name, "", "script" + name, argName, false);
		}
		break;
		}

		return argName;
	}

	// Handle array types
	assert(parameterInformation.TypeInformation.IsArrayOrVector());
	{
		const VariableTypeInformation& arrayElementTypeInformation = parameterInformation.TypeInformation.AssertGetUnderlyingType();

		std::string entryType;
		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::ConstCharString:
		case ExportedClassTypeCategory::Path:
		case ExportedClassTypeCategory::Enum:
			entryType = parameterTypeName;
			break;
		case ExportedClassTypeCategory::MonoObject:
			entryType = "MonoObject*";
			break;
		case ExportedClassTypeCategory::MonoReflectionType:
			entryType = "MonoReflectionType*";
			break;
		default: // Some object or struct type
			entryType = TypeLookup::GetScriptWrapperObjectTypeName(parameterTypeName, arrayElementTypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef));
			break;
		}

		std::string argName = "vec" + name;
		preCallActions << "\t\tMonoArray* " << argName << ";" << std::endl;

		preCallActions << "\t\tint arraySize" << name << " = ";
		if (parameterInformation.TypeInformation.IsArrayOrVector(false))
			preCallActions << "(int)" << name << ".size()";
		else
			preCallActions << parameterInformation.TypeInformation.ArraySize;
		preCallActions << ";\n";

		const std::string scriptArrayName = "array" + name;
		preCallActions << "\t\tScriptArray " << scriptArrayName;
		preCallActions << " = " << "ScriptArray::Create<" << entryType << ">(arraySize" << name << ");" << std::endl;
		preCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
		preCallActions << "\t\t{" << std::endl;

		switch (parameterTypeMappingInformation.TypeCategory)
		{
		case ExportedClassTypeCategory::Primitive:
		case ExportedClassTypeCategory::String:
		case ExportedClassTypeCategory::WString:
		case ExportedClassTypeCategory::Path:
			preCallActions << "\t\t\t" << scriptArrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ExportedClassTypeCategory::ConstCharString:
			outs() << "Error: const char* type not supported an array element. Ignoring. \n";
			break;
		case ExportedClassTypeCategory::Enum:
		{
			std::string enumType;
			ParserUtility::MapBuiltinPrimitiveTypeToCppType(parameterTypeMappingInformation.EnumUnderlyingType, enumType);

			if(arrayElementTypeInformation.TypeCategory == VariableTypeCategory::Flags)
				preCallActions << "\t\t\t" << scriptArrayName << ".Set(i, (" << enumType << ")(uint32_t)" << name << "[i]);" << std::endl;
			else
				preCallActions << "\t\t\t" << scriptArrayName << ".Set(i, (" << enumType << ")" << name << "[i]);" << std::endl;
			break;
		}
		case ExportedClassTypeCategory::Struct:
			preCallActions << "\t\t\t" << scriptArrayName << ".Set(i, ";

			if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				preCallActions << entryType << "::ToInterop(";

			preCallActions << name << "[i]";

			if (arrayElementTypeInformation.IsPostProcessFlagSet(VariablePostProcessFlags::IsStructWrapperUsed))
				preCallActions << ")";

			preCallActions << ");\n";
			break;
		case ExportedClassTypeCategory::MonoObject:
		case ExportedClassTypeCategory::MonoReflectionType:
			preCallActions << "\t\t\t\t" << scriptArrayName << ".Set(i, " << name << "[i]);" << std::endl;
			break;
		case ExportedClassTypeCategory::Class:
		case ExportedClassTypeCategory::ReflectableClass:
		case ExportedClassTypeCategory::IReflectable:
		{
			std::string elemName = "arrayElem" + name;
			preCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
			preCallActions << GenerateNativeClassToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, elemName, entryType, name + "[i]", false, "\t\t\t");
			preCallActions << "\t\t\t" << scriptArrayName << ".Set(i, " << elemName << ");" << std::endl;
		}
		break;
		default: // Some resource or game object type
		{
			preCallActions << GenerateNativeHandleToMonoObject(arrayElementTypeInformation, parameterTypeMappingInformation, name, "i", "script" + name, scriptArrayName, false, "\t\t\t");
		}
		break;
		}

		preCallActions << "\t\t}" << std::endl;
		preCallActions << "\t\t" << argName << " = " << scriptArrayName << ".GetInternal();" << std::endl;

		return argName;
	}
}

/**
 * Generates a body (no signature) for an internal method. Internal methods will be called from scripting and will forward their arguments to the native method they are wrapping.
 * Use GenerateInternalMethodSignature() to generate the corresponding signature.
 *
 * @param classInfo					Information about the class the method is part of.
 * @param methodInfo				Information about the method to generate.
 * @param interopClassName			Name of the interop class we're generating the method on.
 * @param typeMappingInformation	Information about how the native type maps to the script type.
 * @return							Generated method body, with starting/ending { }.
 */
static std::string GenerateInternalMethodBody(const ClassInfo& classInfo, const MethodInfo& methodInfo, const std::string& interopClassName, const TypeMappingInformation& typeMappingInformation)
{
	const bool isModule = classInfo.IsFlagSet(ClassFlags::IsModule);
	const bool isSingleton = classInfo.IsFlagSet(ClassFlags::IsSingleton);

	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = classInfo.IsFlagSet(ClassFlags::IsBase);

	bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);
	bool isCtor = methodInfo.IsFlagSet(MethodFlags::Constructor);
	bool isExternal = methodInfo.IsFlagSet(MethodFlags::External);

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeMappingInformation;
	if (!methodInfo.ReturnValue.TypeInformation.IsEmpty() && !isCtor)
	{
		returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation);
		if (!GeneratorUtility::CanBeReturned(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation))
			returnAsParameter = true;
		else
		{
			std::string returnType = GetCppInteropQualifiedTypeName(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);
			postCallActions << "\t\t" << returnType << " __output;\n";

			std::string argName = GenerateMethodBodyBlockForArgument("__output", methodInfo.ReturnValue, true, true, preCallActions, postCallActions);

			returnAssignment = argName + " = ";
			returnStmt = "\t\treturn __output;";
		}
	}

	preCallActions << GenerateNativeObjectValidityCheck(classInfo, methodInfo);

	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const bool isLastArgument = (I + 1) == methodInfo.Parameters.end();
		const std::string argumentName = GenerateMethodBodyBlockForArgument(I->Name, *I, isLastArgument, false, preCallActions, postCallActions);

		TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(I->TypeInformation);
		methodArgs << GetArgumentForInternalToNativeCall(methodInfo, argumentName, I->TypeInformation, parameterTypeMappingInformation);

		if (!isLastArgument)
			methodArgs << ", ";
	}

	if (returnAsParameter)
	{
		std::string argName = GenerateMethodBodyBlockForArgument("__output", methodInfo.ReturnValue, true, true, preCallActions, postCallActions);

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
			if (typeMappingInformation.IsClassType())
			{
				output << "\t\tSPtr<" << classInfo.NativeName << "> nativeObject = B3DMakeShared<" << classInfo.NativeName << ">(" << methodArgs.str() << ");" << std::endl;
				isValid = true;
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.ExternalClass + "::" + methodInfo.NativeName;

			if (typeMappingInformation.IsClassType())
			{
				output << "\t\tSPtr<" << classInfo.NativeName << "> nativeObject = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				isValid = true;
			}
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
			{
				output << "\t\tTResourceHandle<" << classInfo.NativeName << "> nativeObject = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				isValid = true;
			}
			else if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::GUIElement)
			{
				output << "\t\t" << classInfo.NativeName << "* nativeObject = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				isValid = true;
			}
		}

		if(isValid)
			output << "\t\tScriptObjectWrapper::Create<" << interopClassName << ">(nativeObject, scriptObject);\n";
		else
			outs() << "Error: Cannot generate a constructor for \"" << classInfo.NativeName << "\". Unsupported class type. \n";
	}
	else
	{
		std::stringstream methodCall;
		if (!isExternal)
		{
			if (isStatic)
				methodCall << classInfo.NativeName << "::" << methodInfo.NativeName << "(" << methodArgs.str() << ")"; 
			else if(isModule)
				methodCall << classInfo.NativeName << "::Instance()." << methodInfo.NativeName << "(" << methodArgs.str() << ")";
			else if(isSingleton)
				methodCall << classInfo.SingletonGetterName << "()." << methodInfo.NativeName << "(" << methodArgs.str() << ")";
			else
			{
				VariableTypeInformation typeInformation;
				typeInformation.TypeName = classInfo.NativeName;
				typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

				methodCall << GenerateGetNativeObjectCallLine(typeInformation, typeMappingInformation, "self", false);
				methodCall << "->" << methodInfo.NativeName << "(" << methodArgs.str() << ")";
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.ExternalClass + "::" + methodInfo.NativeName;
			if (isStatic)
				methodCall << fullMethodName << "(" << methodArgs.str() << ")";
			else
			{
				VariableTypeInformation typeInformation;
				typeInformation.TypeName = classInfo.NativeName;
				typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

				methodCall << fullMethodName << "(" << GenerateGetNativeObjectCallLine(typeInformation, typeMappingInformation, "self");

				std::string methodArgsStr = methodArgs.str();
				if (!methodArgsStr.empty())
					methodCall << ", " << methodArgsStr;

				methodCall << ")";
			}
		}

		std::string call;
		if (!methodInfo.ReturnValue.TypeInformation.IsEmpty())
		{
			// Dereference input if needed
			if ((returnTypeMappingInformation.IsClassType() && methodInfo.ReturnValue.TypeInformation.TypeCategory == VariableTypeCategory::General))
			{
				returnAssignment = "*" + returnAssignment;
			}

			call = GetReturnValueForNativeCall(methodCall.str(), methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);
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

/**
 * Generates an internal method body (no signature) that returns a value of a particular field. Use GenerateInternalMethodSignature() to generate the corresponding signature.
 *
 * @param	classInfo				Class the field belongs to.
 * @param	fieldInfo				Information about the field.
 * @param	methodInfo				Method representing the property getter for this field.
 * @param	typeMappingInformation	Information about the field type mapped to script.
 * @return							Contents of the getter interop method, with starting/ending { }.
 */
static std::string GenerateInternalFieldGetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo, const TypeMappingInformation& typeMappingInformation)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	const bool isBase = classInfo.IsFlagSet(ClassFlags::IsBase);
	const bool isModule = classInfo.IsFlagSet(ClassFlags::IsModule);
	const bool isSingleton = classInfo.IsFlagSet(ClassFlags::IsSingleton);
	const bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);

	bool returnAsParameter = false;
	TypeMappingInformation returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInfo.ReturnValue.TypeInformation);
	if (!GeneratorUtility::CanBeReturned(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation))
		returnAsParameter = true;
	else
	{
		std::string returnType = GetCppInteropQualifiedTypeName(methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);
		postCallActions << "\t\t" << returnType << " __output;" << std::endl;

		const std::string argumentName = GenerateMethodBodyBlockForArgument("__output", methodInfo.ReturnValue, true, true, preCallActions, postCallActions);

		returnAssignment = argumentName + " = ";
		returnStmt = "\t\treturn __output;";
	}

	if (returnAsParameter)
	{
		const std::string argumentName = GenerateMethodBodyBlockForArgument("__output", methodInfo.ReturnValue, true, true, preCallActions, postCallActions);

		returnAssignment = argumentName + " = ";
	}

	preCallActions << GenerateNativeObjectValidityCheck(classInfo, methodInfo);

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.NativeName << "::" << fieldInfo.Name; 
	else if(isModule)
		fieldAccess << classInfo.NativeName << "::Instance()." << fieldInfo.Name;
	else if(isSingleton)
		fieldAccess << classInfo.SingletonGetterName << "()." << fieldInfo.Name;
	else
	{
		VariableTypeInformation typeInformation;
		typeInformation.TypeName = classInfo.NativeName;
		typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

		fieldAccess << GenerateGetNativeObjectCallLine(typeInformation, typeMappingInformation, "self", false);
		fieldAccess << "->" << fieldInfo.Name;
	}

	// Dereference input if needed
	if ((returnTypeMappingInformation.IsClassType() && methodInfo.ReturnValue.TypeInformation.TypeCategory == VariableTypeCategory::General))
	{
		returnAssignment = "*" + returnAssignment;
	}

	const std::string access = GetReturnValueForNativeCall(fieldAccess.str(), methodInfo.ReturnValue.TypeInformation, returnTypeMappingInformation);
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

/**
 * Generates an internal method body (no signature) that sets a value of a particular field. Use GenerateInternalMethodSignature() to generate the corresponding signature.
 *
 * @param	classInfo				Class the field belongs to.
 * @param	fieldInfo				Information about the field.
 * @param	methodInfo				Method representing the property setter for this field.
 * @param	typeMappingInformation	Information about the field type mapped to script.
 * @return							Contents of the setter interop method body, with starting/ending { }.
 */
static std::string GenerateInternalFieldSetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo, const TypeMappingInformation& typeMappingInformation)
{
	std::stringstream preCallActions;
	std::stringstream argumentValue;
	std::stringstream postCallActions;

	const bool isBase = classInfo.IsFlagSet(ClassFlags::IsBase);
	const bool isModule = classInfo.IsFlagSet(ClassFlags::IsModule);
	const bool isSingleton = classInfo.IsFlagSet(ClassFlags::IsSingleton);
	const bool isStatic = methodInfo.IsFlagSet(MethodFlags::Static);

	preCallActions << GenerateNativeObjectValidityCheck(classInfo, methodInfo);

	const VariableInformation& parameterInformation = methodInfo.Parameters[0];
	const std::string argumentName = GenerateMethodBodyBlockForArgument(parameterInformation.Name, parameterInformation, false, false, preCallActions, postCallActions);

	const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
	argumentValue << GetArgumentForInternalToNativeCall(methodInfo, argumentName, parameterInformation.TypeInformation, parameterTypeMappingInformation);

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.NativeName << "::" << fieldInfo.Name; 
	else if(isModule)
		fieldAccess << classInfo.NativeName << "::Instance()." << fieldInfo.Name;
	else if(isSingleton)
		fieldAccess << classInfo.SingletonGetterName << "()." << fieldInfo.Name;
	else
	{
		VariableTypeInformation typeInformation;
		typeInformation.TypeName = classInfo.NativeName;
		typeInformation.PostProcessFlags |= isBase ? (uint32_t)VariablePostProcessFlags::IsReferencingBaseClass : 0;

		fieldAccess << GenerateGetNativeObjectCallLine(typeInformation, typeMappingInformation, "self", false);
		fieldAccess << "->" << fieldInfo.Name;
	}

	output << "\t\t" << fieldAccess.str() << " = " << argumentValue.str() << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << "\t}" << std::endl;
	return output.str();
}

/**
 * Generates an internal method body (no signature) that calls a C# thunk when a native event is triggered. Use GenerateEventCallbackSignature() to generate the corresponding signature.
 *
 * @param	classInfo				Class the event belongs to.
 * @param	eventInfo				Information about the event.
 * @return							Contents of the callback interop method body, with starting/ending { }.
 */
static std::string GenerateInternalEventCallbackBody(const ClassInfo& classInfo, const MethodInfo& eventInfo)
{
	std::stringstream preCallActions;
	std::stringstream methodArgs;

	const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();
	const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);

	int idx = 0;
	for (auto I = eventInfo.Parameters.begin(); I != eventInfo.Parameters.end(); ++I)
	{
		const bool isLast = (I + 1) == eventInfo.Parameters.end();
		const std::string argumentName = GenerateEventCallbackBodyBlockForArgument(I->Name, *I, preCallActions);
		const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(I->TypeInformation);

		methodArgs << GetArgumentForInteropEventToThunkCall(eventInfo, argumentName, I->TypeInformation, parameterTypeMappingInformation);

		if (!isLast)
			methodArgs << ", ";

		idx++;
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	output << "\t\tMonoUtil::InvokeThunk(" << eventInfo.NativeName << "Thunk";

	if(!isStatic && !classHasGlobalSingleInstance)
		output << ", GetScriptObject()";
	
	if (!eventInfo.Parameters.empty())
		output << ", " << methodArgs.str();

	output << ");\n";

	output << "\t}" << std::endl;
	return output.str();
}

#pragma endregion C++ Generation Helpers

/**
 * Generates a class declaration (for use in a C++ header file).
 *
 * @param classInfo					Information about the class we're generating.
 * @return							Class declaration.
 */
static std::string GenerateClassDeclaration(const ClassInfo& classInfo)
{
	const bool inEditor = IsAPIEditor(classInfo.API);
	const bool isBase = classInfo.IsFlagSet(ClassFlags::IsBase);
	const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();
	const bool isRootBase = classInfo.BaseClassName.empty();

	bool hasStaticEvents = classHasGlobalSingleInstance && !classInfo.Events.empty();
	bool hasNonStaticEvents = !classHasGlobalSingleInstance && !classInfo.Events.empty();
	if(!hasStaticEvents)
	{
		for(auto& eventInfo : classInfo.Events)
		{
			bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			if(isStatic)
				hasStaticEvents = true;
			else
				hasNonStaticEvents = true;
		}
	}

	std::string dllExportMacro;
	if(!inEditor)
		dllExportMacro = sFrameworkDllExportMacro;
	else
		dllExportMacro = sEditorDllExportMacro;

	auto fnGenerateEventCallbackMethods = [&classInfo](std::stringstream& stream)
	{
		for(auto& eventInfo : classInfo.Events)
		{
			stream << GenerateApiCheckBegin(eventInfo.API);
			stream << "\t\t" << GenerateEventCallbackSignature(classInfo, eventInfo, "") << ";" << std::endl;
			stream << GenerateApiCheckEnd(eventInfo.API);
		}
	};

	auto fnGenerateEventThunks = [&classInfo](std::stringstream& stream)
	{
		const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();
		for(auto& eventInfo : classInfo.Events)
		{
			stream << GenerateApiCheckBegin(eventInfo.API);
			stream << GenerateEventThunkSignature(eventInfo, classHasGlobalSingleInstance);
			stream << GenerateApiCheckEnd(eventInfo.API);
		}
	};

	auto fnGenerateEventHandles = [&classInfo](std::stringstream& stream)
	{
		const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();

		for(auto& eventInfo : classInfo.Events)
		{
			const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			const bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);
			if(!isCallback)
			{
				stream << GenerateApiCheckBegin(eventInfo.API);
				stream << "\t\t";

				if(isStatic || classHasGlobalSingleInstance)
					stream << "static ";

				stream << "HEvent " << eventInfo.NativeName << "Connection;\n";
				stream << GenerateApiCheckEnd(eventInfo.API);
			}
		}
	};

	const TypeMappingInformation& typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(classInfo.NativeName);
	std::string wrappedDataType = GetCppNativeQualifiedTypeName(classInfo.NativeName, typeMappingInformation);
	std::string interopBaseClassName;

	std::stringstream output;
	output << GenerateApiCheckBegin(classInfo.API);

	// Generate a common base class if required
	if(isBase && !classHasGlobalSingleInstance)
	{
		interopBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.NativeName) + "WrapperBase";

		std::string parentBaseClassName;
		std::string parentBaseClassNameWithoutTemplateArguments;
		if(isRootBase)
		{
			parentBaseClassName = GetWrapperRootBaseClass(classInfo.NativeName, typeMappingInformation, true);
			parentBaseClassNameWithoutTemplateArguments = GetWrapperRootBaseClass(classInfo.NativeName, typeMappingInformation, false);
		}
		else
		{
			parentBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.BaseClassName) + "WrapperBase";
			parentBaseClassNameWithoutTemplateArguments = parentBaseClassName;
		}

		output << "\tclass " << dllExportMacro << " " << interopBaseClassName << " : public " << parentBaseClassName << "\n";

		output << "\t{\n";
		output << "\tpublic:\n";
		output << "\t\tusing " << parentBaseClassNameWithoutTemplateArguments << "::" << parentBaseClassNameWithoutTemplateArguments << ";\n";
		output << "\n";

		if(hasNonStaticEvents)
		{
			output << "\t\tvirtual void RegisterEvents();\n";
			output << "\t\tvirtual void UnregisterEvents();\n";
		}

		fnGenerateEventCallbackMethods(output);

		if(!classInfo.Events.empty())
			output << std::endl;

		fnGenerateEventThunks(output);

		if(!classInfo.Events.empty())
			output << std::endl;

		fnGenerateEventHandles(output);

		output << "\t};\n";
		output << "\n";
	}
	else if(!classInfo.BaseClassName.empty())
	{
		interopBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.BaseClassName) + "WrapperBase";
	}

	// Generate main class
	output << "\tclass " << dllExportMacro << " ";

	std::string interopClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.NativeName);
	output << interopClassName << " : public ";

	std::string wrapperTemplatedBaseClass = GetWrapperTemplatedBaseClass(typeMappingInformation, classHasGlobalSingleInstance);
	output << wrapperTemplatedBaseClass << "<";

	if(!classHasGlobalSingleInstance)
	{
		output << classInfo.NativeName << ", " << interopClassName;

		if(!interopBaseClassName.empty())
			output << ", " << interopBaseClassName;
	}
	else
		output << interopClassName;

	output << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if(!inEditor)
		output << "\t\tB3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, \"" << classInfo.ScriptTypeDefinitionTypeName << "\")\n";
	else
		output << "\t\tB3D_SCRIPT_TYPE_DEFINITION(kEditorAssembly, kEditorNs, \"" << classInfo.ScriptTypeDefinitionTypeName << "\")\n";

	output << std::endl;

	// Constructor
	if(!classHasGlobalSingleInstance)
	{
		output << "\t\t" << interopClassName << "(";

		if(typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement) // GUI element only one using raw pointers, should generalize this
			output << "const " << wrappedDataType << "& nativeObject";
		else
			output << wrappedDataType << " nativeObject";

		output << ");\n";
	}
	else
	{
		output << "\t\t" << interopClassName << "();\n";
	}

	// Destructor
	if(!classHasGlobalSingleInstance)
		output << "\t\t" << "~" << interopClassName << "();\n";

	output << "\n";

	// SetupScriptBindings()
	output << "\t\tstatic void SetupScriptBindings();\n";
	output << "\n";

	// Lifetime tracking (if not at default)
	if(classInfo.LifetimeTrackingMode == ScriptObjectLifetimeTrackingMode::ExplicitDestroy)
		output << "\t\tScriptObjectLifetimeTrackingMode GetLifetimeTrackingMode() const override { return ScriptObjectLifetimeTrackingMode::StrongHandleWithExplicitDestroy; }\n";

	if(hasNonStaticEvents && !isBase)
	{
		output << "\t\tvirtual void RegisterEvents();\n";
		output << "\t\tvirtual void UnregisterEvents();\n";
	}

	if(!classHasGlobalSingleInstance)
	{
		// CreateScriptObject() method
		output << "\t\tstatic MonoObject* CreateScriptObject(bool construct);\n";
		output << "\n";
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\t\tstatic void StartUp();" << std::endl;
		output << "\t\tstatic void ShutDown();" << std::endl;
		output << std::endl;
	}

	output << "\tprivate:" << std::endl;

	// Event callback methods
	if(!isBase)
	{
		fnGenerateEventCallbackMethods(output);

		if(!classInfo.Events.empty())
			output << std::endl;
	}

	// Event thunks & handles
	if(!isBase)
	{
		fnGenerateEventThunks(output);
		
		if(!classInfo.Events.empty())
			output << std::endl;

		fnGenerateEventHandles(output);
	}

	if(hasStaticEvents)
		output << std::endl;

	// CLR hooks
	std::string interopClassThisPtrType;
	if (isBase)
		interopClassThisPtrType = interopBaseClassName;
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		output << "\t\tstatic MonoObject* InternalGetRef(" << interopClassThisPtrType << "* self);\n\n";

	for (auto& methodInfo : classInfo.Constructors)
	{
		if (methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t\tstatic " << GenerateInternalMethodSignature(classInfo, methodInfo, interopClassThisPtrType, "") << ";" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.API);
	}

	for (auto& methodInfo : classInfo.Methods)
	{
		if (methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t\tstatic " << GenerateInternalMethodSignature(classInfo, methodInfo, interopClassThisPtrType, "") << ";" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.API);
	}

	output << "\t};" << std::endl;
	output << GenerateApiCheckEnd(classInfo.API);

	return output.str();
}

/**
 * Generates a class definition (for use in a C++ source file).
 *
 * @param classInfo					Information about the class we're generating.
 * @return							Class definition.
 */
static std::string GenerateClassDefinition(const ClassInfo& classInfo)
{
	const bool isBase = classInfo.IsFlagSet(ClassFlags::IsBase);
	const bool isModule = classInfo.IsFlagSet(ClassFlags::IsModule);
	const bool isSingleton = classInfo.IsFlagSet(ClassFlags::IsSingleton);
	const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();

	bool hasStaticEvents = classHasGlobalSingleInstance && !classInfo.Events.empty();
	bool hasNonStaticEvents = !classHasGlobalSingleInstance && !classInfo.Events.empty();
	for(auto& eventInfo : classInfo.Events)
	{
		bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
		if(isStatic)
			hasStaticEvents = true;
		else
			hasNonStaticEvents = true;
	}

	auto fnGenerateEventHandles = [&classInfo](std::stringstream& stream, const std::string& className)
	{
		const bool classHasGlobalSingleInstance = classInfo.HasGlobalSingleInstance();

		bool hasEventHandles = false;
		for(auto& eventInfo : classInfo.Events)
		{
			const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			const bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);
			if(!isCallback && (isStatic || classHasGlobalSingleInstance))
			{
				stream << GenerateApiCheckBegin(eventInfo.API);
				stream << "\tHEvent " << className << "::" << eventInfo.NativeName << "Connection;\n";
				stream << GenerateApiCheckEnd(eventInfo.API);

				hasEventHandles = true;
			}
		}

		if(hasEventHandles)
			stream << "\n";
	};

	auto fnGenerateEventThunks = [&classInfo](std::stringstream& stream, const std::string& className)
	{
		for(auto& eventInfo : classInfo.Events)
		{
			stream << GenerateApiCheckBegin(eventInfo.API);
			stream << "\t" << className << "::" << eventInfo.NativeName << "ThunkDefinition " << className << "::" << eventInfo.NativeName << "Thunk; \n";
			stream << GenerateApiCheckEnd(eventInfo.API);
		}

		if(!classInfo.Events.empty())
			stream << "\n";
	};

	auto fnGenerateEventCallbacks = [&classInfo](std::stringstream& stream, const std::string& className)
	{
		for(auto I = classInfo.Events.begin(); I != classInfo.Events.end(); ++I)
		{
			const MethodInfo& eventInfo = *I;

			stream << GenerateApiCheckBegin(eventInfo.API);
			stream << "\t" << GenerateEventCallbackSignature(classInfo, eventInfo, className) << std::endl;
			stream << GenerateInternalEventCallbackBody(classInfo, eventInfo);
			stream << GenerateApiCheckEnd(eventInfo.API);
			stream << "\n";
		}
	};

	auto fnGenerateRegisterEvents = [&classInfo](std::stringstream& stream, const std::string& className)
	{
		for(auto& eventInfo : classInfo.Events)
		{
			const bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			const bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);
			if(!isStatic)
			{
				stream << GenerateApiCheckBegin(eventInfo.API);

				if(!isCallback)
					stream << "\t\t"<< eventInfo.NativeName << "Connection = static_cast<" << classInfo.NativeName << "*>(GetNativeObject())->" << eventInfo.NativeName << ".Connect(";
				else
					stream << "\t\tstatic_cast<" << classInfo.NativeName << "*>(GetNativeObject())->" << eventInfo.NativeName << " = ";

				stream << "[this](";

				for(int parameterIndex = 0; parameterIndex < (int)eventInfo.Parameters.size(); parameterIndex++)
				{
					const auto& parameterInfo = eventInfo.Parameters[parameterIndex];
					const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInfo.TypeInformation);
					stream << GetCppNativeQualifiedTypeName(parameterInfo.TypeInformation, parameterTypeMappingInformation, false);
					stream << " p" << parameterIndex;

					if(parameterInfo.TypeInformation.TypeCategory == VariableTypeCategory::Array)
						stream << "[" << parameterInfo.TypeInformation.ArraySize << "]";

					if(parameterIndex + 1 < (int)eventInfo.Parameters.size())
						stream << ", ";
				}

				stream << ") { " << eventInfo.InteropName << "(";

				for(int parameterIndex = 0; parameterIndex < (int)eventInfo.Parameters.size(); parameterIndex++)
				{
					stream << "p" << parameterIndex;

					if(parameterIndex + 1 < (int)eventInfo.Parameters.size())
						stream << ", ";
				}

				stream << "); }";

				if(!isCallback)
					stream << ")";

				stream << ");\n";
				stream << GenerateApiCheckEnd(eventInfo.API);
			}
		}
	};

	auto fnGenerateRegisterEventsMethodBody = [&fnGenerateRegisterEvents](std::stringstream& stream, const std::string& className, const std::string& baseClassName)
	{
		stream << "\tvoid " << className << "::RegisterEvents()\n";
		stream << "\t{\n";

		fnGenerateRegisterEvents(stream, className);

		if(!baseClassName.empty())
			stream << "\t\t" << baseClassName << "::RegisterEvents();\n";

		stream << "\t}\n";
	};

	auto fnGenerateUnregisterEventsMethodBody = [](std::stringstream& stream, const ClassInfo& classInfo, std::string& className, const std::string& baseClassName)
	{
		stream << "\tvoid " << className << "::UnregisterEvents()\n";
		stream << "\t{\n";

		for(auto& eventInfo : classInfo.Events)
		{
			const bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);
			if(!isCallback)
			{
				stream << GenerateApiCheckBegin(eventInfo.API);
				stream << "\t\t"<< eventInfo.NativeName << "Connection.Disconnect();\n";
				stream << GenerateApiCheckEnd(eventInfo.API);
			}
		}

		if(!baseClassName.empty())
			stream << "\t\t" << baseClassName << "::UnregisterEvents();\n";

		stream << "\t}\n";
	};

	const TypeMappingInformation& typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(classInfo.NativeName);
	std::string interopClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.NativeName);
	std::string wrappedDataType = GetCppNativeQualifiedTypeName(classInfo.NativeName, typeMappingInformation);

	std::string interopBaseClassName;

	if(isBase)
		interopBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.NativeName) + "WrapperBase";
	else if(!classInfo.BaseClassName.empty())
		interopBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.BaseClassName) + "WrapperBase";

	std::stringstream output;
	output << GenerateApiCheckBegin(classInfo.API);

	if(isBase)
	{
		// Event thunks
		fnGenerateEventThunks(output, interopBaseClassName);

		// Event handles
		fnGenerateEventHandles(output, interopBaseClassName);

		const bool isRootBase = classInfo.BaseClassName.empty();
		std::string parentBaseClassName;

		if(isRootBase)
			parentBaseClassName = GetWrapperRootBaseClass(classInfo.NativeName, typeMappingInformation, true);
		else
			parentBaseClassName = TypeLookup::GetScriptWrapperObjectTypeName(classInfo.BaseClassName) + "WrapperBase";

		// Event callback method implementations
		fnGenerateEventCallbacks(output, interopBaseClassName);

		// (Un)RegisterEvents methods
		if(hasNonStaticEvents)
		{
			fnGenerateRegisterEventsMethodBody(output, interopBaseClassName, parentBaseClassName);
			fnGenerateUnregisterEventsMethodBody(output, classInfo, interopBaseClassName, parentBaseClassName);
		}
	}

	// Event thunks
	if(!isBase)
		fnGenerateEventThunks(output, interopClassName);

	// Event handles
	if(!isBase)
		fnGenerateEventHandles(output, interopClassName);

	// Constructor
	if(!classHasGlobalSingleInstance)
	{
		output << "\t" << interopClassName << "::" << interopClassName << "(";

		if(typeMappingInformation.TypeCategory != ::ExportedClassTypeCategory::GUIElement)
			output << "const " << wrappedDataType << "& nativeObject";
		else
			output << wrappedDataType << " nativeObject";

		output << ")\n";
	}
	else
		output << "\t" << interopClassName << "::" << interopClassName << "()\n";

	output << "\t\t:";

	const std::string wrapperTemplatedBaseClass = GetWrapperTemplatedBaseClass(typeMappingInformation, classHasGlobalSingleInstance);
	output << wrapperTemplatedBaseClass;

	if(!classHasGlobalSingleInstance)
		output << "(nativeObject)";
	else
		output << "()";

	output << std::endl;
	output << "\t{" << std::endl;

	// Register any non-static events
	if(!classHasGlobalSingleInstance)
		output << "\t\tRegisterEvents();\n";

	output << "\t}" << std::endl;
	output << std::endl;

	// Destructor
	if(!classHasGlobalSingleInstance)
	{
		output << "\t" << interopClassName << "::~" << interopClassName << "()\n";
		output << "\t{\n";
		output << "\t\tUnregisterEvents();\n";
		output << "\t}\n";
		output << "\n";
	}

	// CLR hook registration
	output << "\tvoid " << interopClassName << "::SetupScriptBindings()" << std::endl;
	output << "\t{" << std::endl;

	// Internal_GetRef interop method
	if(typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
		output << "\t\tsInteropMetaData.ScriptClass->AddInternalCall(\"Internal_GetRef\", (void*)&" << interopClassName << "::InternalGetRef);\n";

	for(auto& methodInfo : classInfo.Constructors)
	{
		if(methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t\tsInteropMetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.InteropName << "\", (void*)&" << interopClassName << "::Internal" << methodInfo.InteropName << ");" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.API);
	}

	for(auto& methodInfo : classInfo.Methods)
	{
		if(methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t\tsInteropMetaData.ScriptClass->AddInternalCall(\"Internal_" << methodInfo.InteropName << "\", (void*)&" << interopClassName << "::Internal" << methodInfo.InteropName << ");" << std::endl;
		output << GenerateApiCheckEnd(methodInfo.API);
	}

	output << std::endl;

	for(auto& eventInfo : classInfo.Events)
	{
		output << GenerateApiCheckBegin(eventInfo.API);
		output << "\t\t" << eventInfo.NativeName << "Thunk = ";
		output << "(" << eventInfo.NativeName << "ThunkDefinition)sInteropMetaData.ScriptClass->GetMethodExact(";
		output << "\"Internal_" << eventInfo.InteropName << "\", \"";

		for(auto I = eventInfo.Parameters.begin(); I != eventInfo.Parameters.end(); ++I)
		{
			const VariableInformation& paramInfo = *I;
			TypeMappingInformation paramaterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(paramInfo.TypeInformation);

			const std::string signatureTypeName = GetInteropThunkSignatureQualifiedTypeName(paramInfo.TypeInformation, paramaterTypeMappingInformation);

			output << signatureTypeName;

			if((I + 1) != eventInfo.Parameters.end())
				output << ",";
		}

		output << "\")->GetThunk();" << std::endl;
		output << GenerateApiCheckEnd(eventInfo.API);
	}

	output << "\t}" << std::endl;
	output << std::endl;

	// CreateScriptObject(), Create() or CreateInstance() methods
	if ((typeMappingInformation.IsClassType() && !classHasGlobalSingleInstance) || typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource || !classHasGlobalSingleInstance)
	{
		std::stringstream ctorSignature;
		std::stringstream ctorParamsInit;
		MethodInfo unusedCtor = classInfo.FindUnusedConstructorSignature();
		int numDummyParams = (int)unusedCtor.Parameters.size();

		ctorParamsInit << "\t\tbool dummy = false;" << std::endl;
		ctorParamsInit << "\t\tvoid* ctorParams[" << numDummyParams << "] = { ";

		for (int i = 0; i < numDummyParams; i++)
		{
			ctorParamsInit << "&dummy";
			ctorSignature << unusedCtor.Parameters[i].TypeInformation.GetLastWrappedOrSelfTypeName();

			if ((i + 1) < numDummyParams)
			{
				ctorParamsInit << ", ";
				ctorSignature << ",";
			}
		}

		ctorParamsInit << " };" << std::endl;
		ctorParamsInit << std::endl;

		output << "\tMonoObject* " << interopClassName << "::CreateScriptObject(bool construct)\n";

		output << "\t{\n";
		output << ctorParamsInit.str();

		output << "\t\tif(construct)\n";
		output << "\t\t\treturn sInteropMetaData.ScriptClass->CreateInstance(\"" << ctorSignature.str() << "\", ctorParams);\n";
		output << "\n";
		output << "\t\treturn sInteropMetaData.ScriptClass->CreateInstance(false);\n";

		output << "\t}\n";
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\tvoid " << interopClassName << "::StartUp()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.Events)
		{
			bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);
			if (!isCallback)
			{
				if (isStatic)
				{
					output << "\t\t" << eventInfo.NativeName << "Connection = ";
					output << classInfo.NativeName << "::" << eventInfo.NativeName << ".Connect(&" << interopClassName << "::" << eventInfo.InteropName << ");\n";
				}
				else if (isModule)
				{
					output << "\t\t" << eventInfo.NativeName << "Connection = ";
					output << classInfo.NativeName << "::Instance()." << eventInfo.NativeName << ".Connect(&" << interopClassName << "::" << eventInfo.InteropName << ");\n";
				}
				else if(isSingleton)
				{
					output << "\t\t" << eventInfo.NativeName << "Connection = ";
					output << classInfo.SingletonGetterName << "()." << eventInfo.NativeName << ".Connect(&" << interopClassName << "::" << eventInfo.InteropName << ");\n";
				}
			}
			else
			{
				if(isStatic)
					output << classInfo.NativeName << "::" << eventInfo.NativeName << " = &" << interopClassName << "::" << eventInfo.InteropName << ";\n";
				else if(isModule)
					output << classInfo.NativeName << "::Instance()." << eventInfo.NativeName << " = &" << interopClassName << "::" << eventInfo.InteropName << ";\n";
				else if(isSingleton)
					output << classInfo.SingletonGetterName << "()." << eventInfo.NativeName << " = &" << interopClassName << "::" << eventInfo.InteropName << ";\n";
			}
		}

		output << "\t}" << std::endl;

		output << "\tvoid " << interopClassName << "::ShutDown()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.Events)
		{
			bool isStatic = eventInfo.IsFlagSet(MethodFlags::Static);
			bool isCallback = eventInfo.IsFlagSet(MethodFlags::Callback);

			if(!isCallback && (isStatic || classHasGlobalSingleInstance))
				output << "\t\t" << eventInfo.NativeName << "Connection.Disconnect();" << std::endl;
		}

		output << "\t}" << std::endl;
		output << std::endl;
	}

	// Event callback method implementations
	if(!isBase)
		fnGenerateEventCallbacks(output, interopClassName);

	// (Un)registerEvents methods
	if(hasNonStaticEvents && !isBase && !classHasGlobalSingleInstance)
	{
		fnGenerateRegisterEventsMethodBody(output, interopClassName, interopBaseClassName);
		fnGenerateUnregisterEventsMethodBody(output, classInfo, interopClassName, interopBaseClassName);
	}

	// CLR hook method implementations
	std::string interopClassThisPtrType;
	if (isBase)
		interopClassThisPtrType = interopBaseClassName;
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeMappingInformation.TypeCategory == ::ExportedClassTypeCategory::Resource)
	{
		output << "\tMonoObject* " << interopClassName << "::InternalGetRef(" << interopClassThisPtrType << "* self)\n";
		output << "\t{\n";
		if(isBase)
			output << "\t\treturn self->GetOrCreateResourceReference(self->GetBaseNativeObjectAsHandle(), " + classInfo.NativeName + "::GetRttiStatic()->GetRttiId());\n";
		else
			output << "\t\treturn self->GetOrCreateResourceReference();\n";

		output << "\t}\n\n";
	}

	// Constructors
	for (auto I = classInfo.Constructors.begin(); I != classInfo.Constructors.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t" << GenerateInternalMethodSignature(classInfo, methodInfo, interopClassThisPtrType, interopClassName) << std::endl;
		output << GenerateInternalMethodBody(classInfo, methodInfo, interopClassName, typeMappingInformation);
		output << GenerateApiCheckEnd(methodInfo.API);

		if ((I + 1) != classInfo.Methods.end())
			output << std::endl;
	}

	// Methods
	for (auto I = classInfo.Methods.begin(); I != classInfo.Methods.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (methodInfo.IsFlagSet(MethodFlags::CSOnly))
			continue;

		if (methodInfo.IsFlagSet(MethodFlags::FieldWrapper))
			continue;

		output << GenerateApiCheckBegin(methodInfo.API);
		output << "\t" << GenerateInternalMethodSignature(classInfo, methodInfo, interopClassThisPtrType, interopClassName) << std::endl;
		output << GenerateInternalMethodBody(classInfo, methodInfo, interopClassName, typeMappingInformation);
		output << GenerateApiCheckEnd(methodInfo.API);

		if ((I + 1) != classInfo.Methods.end())
			output << std::endl;
	}

	// Field wrapper methods
	for(auto I = classInfo.Fields.begin(); I != classInfo.Fields.end(); ++I)
	{
		const MethodInfo* setterInfo = nullptr;
		const MethodInfo* getterInfo = nullptr;

		std::string getterName = "Get" + I->Name;
		std::string setterName = "Set" + I->Name;
		for(auto& entry : classInfo.Methods)
		{
			if (!entry.IsFlagSet(MethodFlags::FieldWrapper))
				continue;

			if (entry.NativeName == getterName)
				getterInfo = &entry;
			else if (entry.NativeName == setterName)
				setterInfo = &entry;

			if (getterInfo != nullptr && setterInfo != nullptr)
				break;
		}

		assert(getterInfo && setterInfo);

		output << GenerateApiCheckBegin(getterInfo->API);
		output << "\t" << GenerateInternalMethodSignature(classInfo, *getterInfo, interopClassThisPtrType, interopClassName) << std::endl;
		output << GenerateInternalFieldGetterBody(classInfo, *I, *getterInfo, typeMappingInformation);
		output << GenerateApiCheckEnd(getterInfo->API);
		
		output << std::endl;

		output << GenerateApiCheckBegin(setterInfo->API);
		output << "\t" << GenerateInternalMethodSignature(classInfo, *setterInfo, interopClassThisPtrType, interopClassName) << std::endl;
		output << GenerateInternalFieldSetterBody(classInfo, *I, *setterInfo, typeMappingInformation);
		output << GenerateApiCheckEnd(setterInfo->API);
			
		if ((I + 1) != classInfo.Fields.end())
			output << std::endl;
	}

	output << GenerateApiCheckEnd(classInfo.API);

	return output.str();
}

/**
 * Generates a struct declaration (for use in a C++ header file).
 *
 * @param structInfo				Information about the struct we're generating.
 * @return							Struct declaration.
 */
static std::string GenerateStructDeclaration(const StructInfo& structInfo)
{
	const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(structInfo.NativeName);

	std::stringstream output;
	output << GenerateApiCheckBegin(structInfo.API);

	if(structInfo.RequiresInteropType)
	{
		output << "\tstruct " << structInfo.InteropName << "\n";
		output << "\t{\n";

		for(auto& fieldInfo : structInfo.Fields)
		{
			TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInfo.TypeInformation);

			output << "\t\t";
			output << GetCppInteropQualifiedTypeName(fieldInfo.TypeInformation, fieldTypeMappingInformation, true);
			output << " " << fieldInfo.Name << ";\n";
		}

		output << "\t};\n\n";
	}

	output << "\tclass ";

	bool inEditor = IsAPIEditor (structInfo.API);
	if (!inEditor)
		output << sFrameworkDllExportMacro << " ";
	else
		output << sEditorDllExportMacro << " ";

	std::string interopClassName = TypeLookup::GetScriptWrapperObjectTypeName(structInfo.NativeName);
	output << interopClassName << " : public " << "TScriptTypeDefinition<" << interopClassName << ">";

	output << std::endl;
	output << "\t{\n";
	output << "\tpublic:\n";

	if(!inEditor)
		output << "\t\tB3D_SCRIPT_TYPE_DEFINITION(kEngineAssembly, kEngineNs, \"" << structInfo.ScriptTypeDefinitionTypeName << "\")\n";
	else
		output << "\t\tB3D_SCRIPT_TYPE_DEFINITION(kEditorAssembly, kEditorNs, \"" << structInfo.ScriptTypeDefinitionTypeName << "\")\n";

	output << "\n";

	output << "\t\tstatic MonoObject* Box(const " << structInfo.InteropName << "& value);\n";
	output << "\t\tstatic " << structInfo.InteropName << " Unbox(MonoObject* value);\n";

	if(structInfo.RequiresInteropType)
	{
		output << "\t\tstatic " << structInfo.NativeName << " FromInterop(const " << structInfo.InteropName << "& value);\n";
		output << "\t\tstatic " << structInfo.InteropName << " ToInterop(const " << structInfo.NativeName << "& value);\n";
	}

	output << "\n";
	output << "\tprivate:\n";

	// Constructor
	output << "\t\t" << interopClassName << "();\n";
	output << "\n";

	output << "\t};\n";
	output << GenerateApiCheckEnd(structInfo.API);

	return output.str();
}

/**
 * Generates a struct definition (for use in a C++ source file).
 *
 * @param structInfo				Information about the struct we're generating.
 * @return							Struct definition.
 */
std::string GenerateStructDefinition(const StructInfo& structInfo)
{
	const TypeMappingInformation typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(structInfo.NativeName);
	const std::string interopClassName = TypeLookup::GetScriptWrapperObjectTypeName(structInfo.NativeName);

	std::stringstream output;
	output << GenerateApiCheckBegin(structInfo.API);

	// Constructor
	output << "\t" << interopClassName << "::" << interopClassName << "()\n";
	output << "\t{ }\n";
	output << "\n";

	// Box
	output << "\tMonoObject* " << interopClassName << "::Box(const " << structInfo.InteropName << "& value)\n";
	output << "\t{\n";
	output << "\t\treturn MonoUtil::Box(sInteropMetaData.ScriptClass->GetInternalClass(), (void*)&value);\n";
	output << "\t}\n";
	output << "\n";

	// Unbox
	output << "\t" << structInfo.InteropName << " " << interopClassName << "::Unbox(MonoObject* value)\n";
	output << "\t{\n";
	output << "\t\treturn *(" << structInfo.InteropName << "*)MonoUtil::Unbox(value);\n";
	output << "\t}\n";
	output << "\n";

	if(structInfo.RequiresInteropType)
	{
		// Convert from interop
		output << "\t" << structInfo.NativeName << " " << interopClassName << "::FromInterop(const " << structInfo.InteropName << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.NativeName << " output;\n";
		for (auto& fieldInformation : structInfo.Fields)
		{
			if(fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::ConstCharString)
			{
				output << "\t\tB3D_LOG(Error, LogScript, \"const char* type cannot be assigned from scripting for field '" << fieldInformation.Name << "'. This is not supported for this type.\");\n";
				continue;
			}

			// Arrays can be assigned, so copy them entry by entry
			if(fieldInformation.TypeInformation.TypeCategory == VariableTypeCategory::Array)
			{
				const std::string argumentVariableName = GenerateFieldConvertBlock(fieldInformation.Name, fieldInformation, false, output);

				output << "\t\tauto tmp" << fieldInformation.Name << " = " << argumentVariableName << ";\n";
				output << "\t\tfor(int i = 0; i < " << fieldInformation.TypeInformation.ArraySize << "; ++i)\n";
				output << "\t\t\toutput." << fieldInformation.Name << "[i] = tmp" << fieldInformation.Name << "[i];\n";
			}
			else
			{
				const std::string argumentVariableName = GenerateFieldConvertBlock(fieldInformation.Name, fieldInformation, false, output);

				output << "\t\toutput." << fieldInformation.Name << " = " << argumentVariableName << ";\n";
			}
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";

		// Convert to interop
		output << "\t" << structInfo.InteropName << " " << interopClassName << "::ToInterop(const " << structInfo.NativeName << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.InteropName << " output;\n";
		for(auto& fieldInfo : structInfo.Fields)
		{
			const std::string argumentVariableName = GenerateFieldConvertBlock(fieldInfo.Name, fieldInfo, true, output);

			output << "\t\toutput." << fieldInfo.Name << " = " << argumentVariableName << ";\n";
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";
	}

	output << GenerateApiCheckEnd(structInfo.API);
	return output.str();
}

/**
 * Generates all the script interop C++ code.
 *
 * @param engineOutputFolder		Folder in which to output engine/framework files.
 * @param editorOutputFolder		Folder in which to output editor-specific files.
 * @param generateEditor			True if generating code for the editor. Editor specific files will be ignored if alse.
 */
void GenerateCpp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditor)
{
	GeneratorUtility::CleanAndPrepareFolder(engineOutputFolder);

	if(generateEditor)
	{
		GeneratorUtility::CleanAndPrepareFolder(editorOutputFolder);
	}

	// Generate .h
	for (auto& fileInfo : TypeLookup::GetFilesToGenerate())
	{
		if(fileInfo.second.InEditor && !generateEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.Classes;
		auto& structInfos = fileInfo.second.Structs;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			const ClassInfo& classInfo = *I;
			body << GenerateClassDeclaration(classInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			const StructInfo& structInfo = *I;
			body << GenerateStructDeclaration(structInfo);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.InEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = GeneratorUtility::CreateFile("B3DScript" + fileInfo.first + ".generated.h", cppOutputFolder);

		// License/copyright header
		output << GeneratorUtility::GenerateCopyrightHeader(fileInfo.second.InEditor);

		output << "#pragma once" << std::endl;
		output << std::endl;

		// Output includes
		for (auto& include : fileInfo.second.ReferencedHeaderIncludes)
			output << "#include \"" << GeneratorUtility::GetRelativePath(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		// Output forward declarations
		for (auto& decl : fileInfo.second.ForwardDeclarations)
		{
			for (auto& nsEntry : decl.Namespace)
				output << "namespace " << nsEntry << " { ";
			
			if (decl.TemplateParameters.size() > 0)
			{
				output << "template<";

				for (int i = 0; i < (int)decl.TemplateParameters.size(); ++i)
				{
					if (i != 0)
						output << ", ";

					output << decl.TemplateParameters[i].Kind << " T" << std::to_string(i);
				}

				output << "> ";
			}

			if (decl.IsStruct)
				output << "struct " << decl.TypeName << ";";
			else
				output << "class " << decl.TypeName << ";";

			for (auto& nsEntry : decl.Namespace)
				output << " }";
			
			output << "\n";
		}

		output << "namespace " << (fileInfo.second.InEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate .cpp
	for (auto& fileInfo : TypeLookup::GetFilesToGenerate())
	{
		if(fileInfo.second.InEditor && !generateEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.Classes;
		auto& structInfos = fileInfo.second.Structs;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			const ClassInfo& classInfo = *I;
			body << GenerateClassDefinition(classInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			body << GenerateStructDefinition(*I);

			if ((I + 1) != structInfos.end())
				body << std::endl;
		}

		StringRef cppOutputFolder = fileInfo.second.InEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = GeneratorUtility::CreateFile("B3DScript" + fileInfo.first + ".generated.cpp", cppOutputFolder);

		// License/copyright header
		output << GeneratorUtility::GenerateCopyrightHeader(fileInfo.second.InEditor);

		// Output includes
		for (auto& include : fileInfo.second.ReferencedSourceIncludes)
			output << "#include \"" << GeneratorUtility::GetRelativePath(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		output << "namespace " << (fileInfo.second.InEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}
}
