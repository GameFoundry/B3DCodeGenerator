#include "B3DTypeLookup.h"

std::unordered_map<std::string, FileInfo> TypeLookup::mFilesToGenerate;

StructInfo* TypeLookup::FindStructInformation(const std::string& name)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& structInfo : fileInfo.second.Structs)
		{
			if (structInfo.NativeName == name)
				return &structInfo;
		}
	}

	return nullptr;
}

ClassInfo* TypeLookup::FindClassInformation(const std::string& name, bool isEditor)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& classInfo : fileInfo.second.Classes)
		{
			if (classInfo.NativeName != name)
				continue;

			// Two versions of editor and Framework class migth exist, make sure to pick the right one
			if((isEditor && classInfo.API == ApiFlags::Framework) || (!isEditor &&  IsAPIEditor(classInfo.API)))
				continue;

			return &classInfo;
		}
	}

	return nullptr;
}

EnumInfo* TypeLookup::FindEnumInformation(const std::string& name)
{
	for (auto& fileInfo : mFilesToGenerate)
	{
		for (auto& enumInfo : fileInfo.second.Enums)
		{
			if (enumInfo.NativeName == name)
				return &enumInfo;
		}
	}

	return nullptr;
}

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
				bool isConstructor = (entry.MethodFlags & (int)MethodFlags::Constructor) != 0;
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

