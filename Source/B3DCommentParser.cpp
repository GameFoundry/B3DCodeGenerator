#include "B3DCommentParser.h"
#include "B3DParserUtility.h"
#include "B3DGeneratorUtility.h"

bool CommentParser::ParseComments(const Decl* decl, CommentEntry& output)
{
	assert(decl != nullptr);
	assert(mASTContext != nullptr);

	comments::FullComment* comment = mASTContext->getCommentForDecl(decl, nullptr);
	if (comment == nullptr)
		return false;

	const comments::CommandTraits& traits = mASTContext->getCommentCommandTraits();

	comments::BlockCommandComment* brief = nullptr;
	comments::BlockCommandComment* returns = nullptr;
	std::vector<comments::ParagraphComment*> headerParagraphs;
	SmallVector<comments::ParamCommandComment*, 5> params;

	auto commentIter = comment->child_begin();
	while (commentIter != comment->child_end())
	{
		comments::Comment* childComment = *commentIter;
		int kind = childComment->getCommentKind();

		switch (kind)
		{
		case comments::Comment::CommentKind::NoCommentKind:
			break;
		case comments::Comment::CommentKind::BlockCommandCommentKind:
		{
			comments::BlockCommandComment* blockComment = cast<comments::BlockCommandComment>(childComment);
			const comments::CommandInfo *commandInfo = traits.getCommandInfo(blockComment->getCommandID());

			if (brief == nullptr && commandInfo->IsBriefCommand)
				brief = blockComment;

			if (returns == nullptr && commandInfo->IsReturnsCommand)
				returns = blockComment;

			break;
		}
		case comments::Comment::CommentKind::ParagraphCommentKind:
		{
			comments::ParagraphComment* paragraphComment = cast<comments::ParagraphComment>(childComment);

			if (!paragraphComment->isWhitespace())
				headerParagraphs.push_back(paragraphComment);

			break;
		}
		case comments::Comment::CommentKind::ParamCommandCommentKind:
		{
			comments::ParamCommandComment* paramComment = cast<comments::ParamCommandComment>(childComment);

			if (paramComment->hasParamName() && paramComment->hasNonWhitespaceParagraph())
				params.push_back(paramComment);

			break;
		}
		}

		++commentIter;
	}

	bool hasAnyData = false;
	auto parseParagraphComments = [&traits, &hasAnyData, this](const std::vector<comments::ParagraphComment*>& paragraphs, 
		SmallVector<CommentParagraph, 2>& output)
	{
		auto getTrimmedText = [](const StringRef& input, std::stringstream& output)
		{
			bool lastIsSpace = false;
			for (auto& entry : input)
			{
				if (lastIsSpace)
				{
					if (entry == ' ' || entry == '\t')
						continue;

					output << entry;
					lastIsSpace = false;
				}
				else
				{
					if (entry == ' ')
						lastIsSpace = true;

					if (entry == '\t')
					{
						output << " ";
						lastIsSpace = true;
					}
					else
						output << entry;
				}
			}
		};

		int nativeDoc = 0;
		for (auto& paragraph : paragraphs)
		{
			CommentParagraph commentText;

			std::stringstream paragraphText;
			auto childIter = paragraph->child_begin();

			uint32_t refsTotalSize = 0;
			while (childIter != paragraph->child_end())
			{
				comments::Comment* childComment = *childIter;
				int kind = childComment->getCommentKind();

				if (kind == comments::Comment::CommentKind::TextCommentKind)
				{
					if (nativeDoc <= 0)
					{
						comments::TextComment* textCommand = cast<comments::TextComment>(childComment);

						StringRef text = textCommand->getText();
						if (text.empty())
						{
							++childIter;
							continue;
						}

						getTrimmedText(text, paragraphText);
						hasAnyData = true;
					}
				}
				else if (kind == comments::Comment::CommentKind::InlineCommandCommentKind)
				{
					comments::InlineCommandComment* inlineCommand = cast<comments::InlineCommandComment>(childComment);

					std::string name = inlineCommand->getCommandName(traits).str();
					if (name == "copydoc")
					{
						if(nativeDoc <= 0 && inlineCommand->getNumArgs() > 0)
						{
							const StringRef argument = inlineCommand->getArgText(0);
							commentText.Text = "@copydoc " + argument.str();

							// Note: We don't support any other comment along with copydoc at the moment
							output.push_back(commentText);
							break;
						}
					}
					else if (name == "native")
						nativeDoc++;
					else if (name == "endnative")
						nativeDoc--;
					else if(name == "p" || name == "see")
					{
						if(nativeDoc <= 0 && inlineCommand->getNumArgs() > 0)
						{
							int orgg = paragraphText.tellg();
							paragraphText.seekg(0, std::ios::end);
							int size = paragraphText.tellg();
							paragraphText.seekg(orgg, std::ios::beg);

							CommentReference ref;
							ref.PositionInText = size + refsTotalSize;

							StringRef refArg = inlineCommand->getArgText(0);
							if (refArg.endswith(".") || refArg.endswith(","))
							{
								paragraphText << refArg[refArg.size() - 1];
								refArg = refArg.substr(0, refArg.size() - 1);
							}

							ref.Name = refArg.str();

							if (name == "p")
								commentText.ParameterReferences.push_back(ref);
							else if (name == "see")
								commentText.DeclarationReferences.push_back(ref);

							refsTotalSize += ref.Name.size();
						}
					}
				}

				++childIter;
			}

			std::string paragraphStr = paragraphText.str();
			StringRef trimmedText(paragraphStr.data(), paragraphStr.length());

			size_t leftTrimmedCount = trimmedText.find_first_not_of(" \t\n\v\f\r");
			if(leftTrimmedCount != StringRef::npos)
			{
				for (auto& entry : commentText.ParameterReferences)
					entry.PositionInText -= leftTrimmedCount;

				for (auto& entry : commentText.DeclarationReferences)
					entry.PositionInText -= leftTrimmedCount;
			}
			
			trimmedText = trimmedText.trim();

			if (!trimmedText.empty() || !commentText.ParameterReferences.empty() || !commentText.DeclarationReferences.empty())
			{
				commentText.Text = trimmedText.str();
				output.push_back(commentText);
			}
		}
	};

	if (brief != nullptr)
		parseParagraphComments({ brief->getParagraph() }, output.Brief);

	parseParagraphComments(headerParagraphs, output.Brief);

	for (auto& entry : params)
	{
		CommentParameterEntry paramEntry;

		if (entry->isParamIndexValid())
			paramEntry.Name = entry->getParamName(comment).str();
		else
			paramEntry.Name = entry->getParamNameAsWritten().str();

		parseParagraphComments({ entry->getParagraph() }, paramEntry.Comments);

		output.ParameterComments.push_back(paramEntry);
	}

	if (returns != nullptr)
		parseParagraphComments({ returns->getParagraph() }, output.ReturnValueComments);

	return hasAnyData;
}

void CommentParser::ParseCommentMethodInfo(const FunctionDecl* decl, CommentInformation& commentInfo) const
{
	assert(decl != nullptr);
	assert(mASTContext != nullptr);

	const FunctionProtoType* ft = nullptr;
	if (decl->hasWrittenPrototype())
		ft = dyn_cast<FunctionProtoType>(decl->getType()->castAs<FunctionProtoType>());

	MethodCommentInformation methodInfo;
	if (ft)
	{
		std::string currentNS = ParserUtility::GetNamespace(decl);
		std::string constQualifier = "const ";

		const unsigned numParams = decl->getNumParams();
		for (unsigned i = 0; i < numParams; ++i)
		{
			QualType type = decl->getParamDecl(i)->getType();

			std::stringstream typeStream;
			std::string typeName = type.getAsString(mASTContext->getPrintingPolicy());

			const std::string::size_type constPos = typeName.find(constQualifier);
			bool hasConst = false;
			if (constPos != std::string::npos)
			{
				typeName.erase(constPos, constQualifier.length());
				hasConst = true;
			}

			typeName.erase(std::remove_if(typeName.begin(), typeName.end(), [](const char& val)
			{
				return isspace(val) || val == '&' || val == '*';
			}), typeName.end());

			const std::string::size_type nsPos = typeName.find(currentNS);
			if (nsPos != std::string::npos)
				typeName.erase(nsPos, currentNS.length());

			if (hasConst)
				typeStream << "const ";

			typeStream << typeName;

			if (type->isReferenceType())
				typeStream << "&";
			else if (type->isPointerType())
				typeStream << "*";

			methodInfo.ParameterNames.push_back(typeStream.str());
		}
	}

	commentInfo.Overloads.push_back(methodInfo);
}

void CommentParser::ParseCommentInfo(const NamedDecl* decl, CommentInformation& commentInfo)
{
	commentInfo.IsMethod = false;

	const DeclContext* context = decl->getDeclContext();
	SmallVector<const NamedDecl *, 8> contexts;

	// Collect contexts
	if(dyn_cast<NamedDecl>(context) != decl)
		contexts.push_back(decl);

	while (context && isa<NamedDecl>(context)) 
	{
		contexts.push_back(dyn_cast<NamedDecl>(context));
		context = context->getParent();
	}

	SmallVector<std::string, 2> typeName;
	for (const NamedDecl* dc : reverse(contexts))
	{
		if (const auto* nd = dyn_cast<NamespaceDecl>(dc)) 
		{
			if (!nd->isAnonymousNamespace())
				commentInfo.Namespaces.push_back(nd->getDeclName().getAsString());
		}
		else if (const auto* rd = dyn_cast<RecordDecl>(dc)) 
		{
			if (rd->getIdentifier())
				typeName.push_back(rd->getDeclName().getAsString());
		}
		else if (const auto* fd = dyn_cast<FunctionDecl>(dc)) 
		{
			ParseCommentMethodInfo(fd, commentInfo);

			typeName.push_back(fd->getDeclName().getAsString());
			commentInfo.IsMethod = true;
		}
		else if (const auto* ed = dyn_cast<EnumDecl>(dc)) {
			if (ed->isScoped() || ed->getIdentifier())
				typeName.push_back(ed->getDeclName().getAsString());
		}
		else 
		{
			typeName.push_back(cast<NamedDecl>(dc)->getDeclName().getAsString());
		}
	}

	std::stringstream typeNameStream;
	for(int i = 0; i < (int)typeName.size(); i++)
	{
		if (i > 0)
			typeNameStream << "::";

		typeNameStream << typeName[i];
	}

	commentInfo.TypeName = typeNameStream.str();

	std::stringstream fullTypeNameStream;
	for(int i = 0; i < (int)commentInfo.Namespaces.size(); i++)
	{
		if (i > 0)
			fullTypeNameStream << "::";

		fullTypeNameStream << commentInfo.Namespaces[i];
	}

	fullTypeNameStream << "::" << commentInfo.TypeName;
	commentInfo.FullName = fullTypeNameStream.str();
}

void CommentParser::LookupOrParseComments(const NamedDecl* decl, CommentInformation& commentInfo)
{
	auto iterFind = mCommentLookupViaFullName.find(commentInfo.FullName);
	if (iterFind == mCommentLookupViaFullName.end())
	{
		bool hasComment;
		if (commentInfo.IsMethod)
			hasComment = ParseComments(decl, commentInfo.Overloads[0].Comment);
		else
			hasComment = ParseComments(decl, commentInfo.Comment);

		if (!hasComment)
			return;

		mCommentLookupViaFullName[commentInfo.FullName] = (int)mCommentTypeInformation.size();

		SmallVector<int, 2>& entries = mCommentLookupViaTypeName[commentInfo.TypeName];
		entries.push_back((int)mCommentTypeInformation.size());

		mCommentTypeInformation.push_back(commentInfo);
	}
	else if(commentInfo.IsMethod) // Can be an overload
	{
		CommentInformation& existingInfo = mCommentTypeInformation[iterFind->second];

		bool foundExisting = false;
		for(auto& paramInfo : existingInfo.Overloads)
		{
			int numParams = paramInfo.ParameterNames.size();
			if (numParams != commentInfo.Overloads[0].ParameterNames.size())
				continue;

			bool paramsMatch = true;
			for(int i = 0; i < numParams; i++)
			{
				if(paramInfo.ParameterNames[i] != commentInfo.Overloads[0].ParameterNames[i])
				{
					paramsMatch = false;
					break;
				}
			}

			if(paramsMatch)
			{
				foundExisting = true;
				break;
			}
		}

		if(!foundExisting)
		{
			bool hasComment = ParseComments(decl, commentInfo.Overloads[0].Comment);
			if (hasComment)
				existingInfo.Overloads.push_back(commentInfo.Overloads[0]);
		}
	}
}

void CommentParser::ParseAndRegisterAllComments(const CXXRecordDecl* decl)
{
	assert(decl != nullptr);

	if (!decl->isCompleteDefinition())
		return;

	CommentInformation commentInfo;
	ParseCommentInfo(decl, commentInfo);
	LookupOrParseComments(decl, commentInfo);

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		for (auto I = curDecl->method_begin(); I != curDecl->method_end(); ++I)
		{
			if (I->isImplicit())
				continue;

			if (const auto* fd = dyn_cast<FunctionDecl>(*I))
			{
				CommentInformation methodCommentInfo;
				methodCommentInfo.IsMethod = true;
				methodCommentInfo.Namespaces = commentInfo.Namespaces;
				methodCommentInfo.TypeName = commentInfo.TypeName + "::" + I->getDeclName().getAsString();
				methodCommentInfo.FullName = commentInfo.FullName + "::" + I->getDeclName().getAsString();

				ParseCommentMethodInfo(fd, methodCommentInfo);
				LookupOrParseComments(fd, methodCommentInfo);
			}
		}

		for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
		{
			if (const auto* fd = dyn_cast<FieldDecl>(*I))
			{
				CommentInformation fieldCommentInfo;
				fieldCommentInfo.IsMethod = false;
				fieldCommentInfo.Namespaces = commentInfo.Namespaces;
				fieldCommentInfo.TypeName = commentInfo.TypeName + "::" + I->getDeclName().getAsString();
				fieldCommentInfo.FullName = commentInfo.FullName + "::" + I->getDeclName().getAsString();

				LookupOrParseComments(fd, fieldCommentInfo);
			}
		}

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			if(baseDecl != nullptr)
				todo.push(baseDecl);

			iter++;
		}
	}
}

void CommentParser::ParseAndRegisterAllComments(const EnumDecl* decl)
{
	assert(decl != nullptr);

	CommentInformation commentInfo;
	ParseCommentInfo(decl, commentInfo);
	LookupOrParseComments(decl, commentInfo);
}

bool CommentParser::TryLookupComment(const std::string& value, const std::string& parentType, const SmallVector<std::string, 4>& currentNamespace, CommentEntry& outputComment)
{
	StringRef inputStr(value.data(), value.length());
	inputStr = inputStr.trim();

	bool hasParamList = inputStr.find('(') != -1;

	StringRef fullTypeName;
	StringRef params;

	if (hasParamList)
	{
		auto paramSplit = inputStr.split('(');

		fullTypeName = paramSplit.first.trim();
		params = paramSplit.second.trim(") \t\n\v\f\r");
	}
	else
	{
		fullTypeName = inputStr;
	}

	SmallVector<StringRef, 4> typeSplits;
	fullTypeName.split(typeSplits, "::", -1, false);

	if (typeSplits.empty())
		typeSplits.push_back(fullTypeName);

	// Find matching type (no namespace)
	int namespaceStart = -1;
	std::string simpleTypeName;
	SmallVector<int, 2> lookup;

	if (typeSplits.size() > 1)
	{
		simpleTypeName = typeSplits[typeSplits.size() - 2].str() + "::" + typeSplits[typeSplits.size() - 1].str();
		namespaceStart = 2;

		auto iterFind = mCommentLookupViaTypeName.find(simpleTypeName);
		if (iterFind == mCommentLookupViaTypeName.end())
		{
			simpleTypeName = typeSplits[typeSplits.size() - 1].str();
			iterFind = mCommentLookupViaTypeName.find(simpleTypeName);
			namespaceStart = 1;
		}

		if (iterFind == mCommentLookupViaTypeName.end())
		{
			outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
			return false;
		}
		else
			lookup = iterFind->second;
	}
	else
	{
		simpleTypeName = typeSplits[typeSplits.size() - 1].str();
		namespaceStart = 1;

		auto iterFind = mCommentLookupViaTypeName.find(simpleTypeName);
		if (iterFind == mCommentLookupViaTypeName.end())
		{
			// Try appending the parent type
			simpleTypeName = parentType + "::" + simpleTypeName;

			iterFind = mCommentLookupViaTypeName.find(simpleTypeName);
			if (iterFind == mCommentLookupViaTypeName.end())
			{
				outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
				return false;
			}
			else
				lookup = iterFind->second;
		}
		else
			lookup = iterFind->second;
	}

	// Confirm namespace matches
	SmallVector<std::string, 4> copydocNS;
	for (int i = 0; i < (int)(typeSplits.size() - namespaceStart); i++)
		copydocNS.push_back(typeSplits[i].str());

	SmallVector<std::string, 4> fullNS;
	for (int i = 0; i < (int)currentNamespace.size(); i++)
		fullNS.push_back(currentNamespace[i]);

	for (int i = 0; i < (int)copydocNS.size(); i++)
		fullNS.push_back(copydocNS[i]);

	// First try to assume @copydoc specified namespace is relative to current NS
	int entryMatch = -1;
	for (int i = 0; i < (int)lookup.size(); i++)
	{
		CommentInformation& curCommentInfo = mCommentTypeInformation[lookup[i]];

		if (fullNS.size() != curCommentInfo.Namespaces.size())
			continue;

		bool matches = true;
		for (int j = 0; j < (int)curCommentInfo.Namespaces.size(); j++)
		{
			if (fullNS[j] != curCommentInfo.Namespaces[j])
			{
				matches = false;
				break;
			}
		}

		if (matches)
		{
			entryMatch = i;
			break;
		}
	}

	// If nothing is found, assume provided namespace is global
	if (entryMatch == -1)
	{
		for (int i = 0; i < (int)lookup.size(); i++)
		{
			CommentInformation& curCommentInfo = mCommentTypeInformation[lookup[i]];

			if (copydocNS.size() != curCommentInfo.Namespaces.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)curCommentInfo.Namespaces.size(); j++)
			{
				if (copydocNS[j] != curCommentInfo.Namespaces[j])
				{
					matches = false;
					break;
				}
			}

			if (matches)
			{
				entryMatch = i;
				break;
			}
		}
	}

	if (entryMatch == -1)
	{
		outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
		return false;
	}

	CommentInformation& finalCommentInfo = mCommentTypeInformation[lookup[entryMatch]];
	if (hasParamList)
	{
		if (!finalCommentInfo.IsMethod)
		{
			outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
			return false;
		}

		SmallVector<StringRef, 8> paramSplits;
		params.split(paramSplits, ",", -1, false);

		for (int i = 0; i < (int)paramSplits.size(); i++)
			paramSplits[i] = paramSplits[i].trim();

		int overloadMatch = -1;
		for (int i = 0; i < (int)finalCommentInfo.Overloads.size(); i++)
		{
			if (paramSplits.size() != finalCommentInfo.Overloads[i].ParameterNames.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)paramSplits.size(); j++)
			{
				if (paramSplits[j] != finalCommentInfo.Overloads[i].ParameterNames[j])
				{
					matches = false;
					break;
				}
			}

			if (matches)
			{
				overloadMatch = i;
				break;
			}
		}

		if (overloadMatch == -1)
		{
			// Assume the user doesn't care which overload is used
			if (paramSplits.empty())
				overloadMatch = 0;
			else
			{
				outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
				return false;
			}
		}

		outputComment = finalCommentInfo.Overloads[overloadMatch].Comment;
		return true;
	}

	if (finalCommentInfo.IsMethod)
		outputComment = finalCommentInfo.Overloads[0].Comment;
	else
		outputComment = finalCommentInfo.Comment;

	return true;
}

void CommentParser::ResolveCopydocComments(CommentEntry& comment, const std::string& parentType, const SmallVector<std::string, 4>& currentNamespace)
{
	StringRef copydocArg;
	for (auto& entry : comment.Brief)
	{
		StringRef commentRef(entry.Text.data(), entry.Text.length());

		if (commentRef.startswith("@copydoc"))
		{
			copydocArg = commentRef.split(' ').second;
			break;
		}
	}

	if (copydocArg.empty())
		return;

	CommentEntry outComment;
	if (!TryLookupComment(copydocArg.str(), parentType, currentNamespace, outComment))
	{
		comment = CommentEntry();
		return;
	}
	else
	{
		comment = outComment;
	}

	ResolveCopydocComments(comment, parentType, currentNamespace);
}

void CommentParser::EnsureValidParameterReferenceComments(const std::vector<VariableInformation>& paramInfos, CommentParagraph& comment)
{
	for(auto iter = comment.ParameterReferences.begin(); iter != comment.ParameterReferences.end();)
	{
		const CommentReference& entry = *iter;

		auto iterFind = std::find_if(paramInfos.begin(), paramInfos.end(), 
			[&entry](const VariableInformation& varInfo)
		{
			return entry.Name == varInfo.Name;
		});

		if (iterFind == paramInfos.end())
		{
			comment.DeclarationReferences.push_back(entry);
			iter = comment.ParameterReferences.erase(iter);
		}
		else
			++iter;
	}
}

void CommentParser::EnsureValidParameterReferenceComments(const std::vector<VariableInformation>& paramInfos, CommentEntry& comment)
{
	for (auto& entry : comment.Brief)
		EnsureValidParameterReferenceComments(paramInfos, entry);

	for (auto& entry : comment.ParameterComments)
	{
		for(auto& textEntry : entry.Comments)
			EnsureValidParameterReferenceComments(paramInfos, textEntry);
	}

	for (auto& entry : comment.ReturnValueComments)
		EnsureValidParameterReferenceComments(paramInfos, entry);
}

void CommentParser::ClearParameterReferenceComments(CommentEntry& comment)
{
	EnsureValidParameterReferenceComments({}, comment);
}
