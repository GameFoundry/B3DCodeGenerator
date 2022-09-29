#include "B3DCommentParser.h"

#include "B3DParserUtility.h"

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
		SmallVector<CommentText, 2>& output)
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
			CommentText commentText;

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
		parseParagraphComments({ brief->getParagraph() }, output.brief);

	parseParagraphComments(headerParagraphs, output.brief);

	for (auto& entry : params)
	{
		CommentParameterEntry paramEntry;

		if (entry->isParamIndexValid())
			paramEntry.Name = entry->getParamName(comment).str();
		else
			paramEntry.Name = entry->getParamNameAsWritten().str();

		parseParagraphComments({ entry->getParagraph() }, paramEntry.comments);

		output.params.push_back(paramEntry);
	}

	if (returns != nullptr)
		parseParagraphComments({ returns->getParagraph() }, output.returns);

	return hasAnyData;
}

void CommentParser::ParseCommentMethodInfo(const FunctionDecl* decl, CommentInformation& commentInfo) const
{
	assert(decl != nullptr);
	assert(mASTContext != nullptr);

	const FunctionProtoType* ft = nullptr;
	if (decl->hasWrittenPrototype())
		ft = dyn_cast<FunctionProtoType>(decl->getType()->castAs<FunctionProtoType>());

	CommentMethodInformation methodInfo;
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

			methodInfo.params.push_back(typeStream.str());
		}
	}

	commentInfo.overloads.push_back(methodInfo);
}

void CommentParser::ParseCommentInfo(const NamedDecl* decl, CommentInformation& commentInfo)
{
	commentInfo.isFunction = false;

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
				commentInfo.namespaces.push_back(nd->getDeclName().getAsString());
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
			commentInfo.isFunction = true;
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

	commentInfo.name = typeNameStream.str();

	std::stringstream fullTypeNameStream;
	for(int i = 0; i < (int)commentInfo.namespaces.size(); i++)
	{
		if (i > 0)
			fullTypeNameStream << "::";

		fullTypeNameStream << commentInfo.namespaces[i];
	}

	fullTypeNameStream << "::" << commentInfo.name;
	commentInfo.fullName = fullTypeNameStream.str();
}

void CommentParser::LookupOrParseComments(const NamedDecl* decl, CommentInformation& commentInfo)
{
	auto iterFind = mCommentLookupViaFullName.find(commentInfo.fullName);
	if (iterFind == mCommentLookupViaFullName.end())
	{
		bool hasComment;
		if (commentInfo.isFunction)
			hasComment = ParseComments(decl, commentInfo.overloads[0].comment);
		else
			hasComment = ParseComments(decl, commentInfo.comment);

		if (!hasComment)
			return;

		mCommentLookupViaFullName[commentInfo.fullName] = (int)mCommentTypeInformation.size();

		SmallVector<int, 2>& entries = mCommentLookupViaTypeName[commentInfo.name];
		entries.push_back((int)mCommentTypeInformation.size());

		mCommentTypeInformation.push_back(commentInfo);
	}
	else if(commentInfo.isFunction) // Can be an overload
	{
		CommentInformation& existingInfo = mCommentTypeInformation[iterFind->second];

		bool foundExisting = false;
		for(auto& paramInfo : existingInfo.overloads)
		{
			int numParams = paramInfo.params.size();
			if (numParams != commentInfo.overloads[0].params.size())
				continue;

			bool paramsMatch = true;
			for(int i = 0; i < numParams; i++)
			{
				if(paramInfo.params[i] != commentInfo.overloads[0].params[i])
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
			bool hasComment = ParseComments(decl, commentInfo.overloads[0].comment);
			if (hasComment)
				existingInfo.overloads.push_back(commentInfo.overloads[0]);
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
				methodCommentInfo.isFunction = true;
				methodCommentInfo.namespaces = commentInfo.namespaces;
				methodCommentInfo.name = commentInfo.name + "::" + I->getDeclName().getAsString();
				methodCommentInfo.fullName = commentInfo.fullName + "::" + I->getDeclName().getAsString();

				ParseCommentMethodInfo(fd, methodCommentInfo);
				LookupOrParseComments(fd, methodCommentInfo);
			}
		}

		for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
		{
			if (const auto* fd = dyn_cast<FieldDecl>(*I))
			{
				CommentInformation fieldCommentInfo;
				fieldCommentInfo.isFunction = false;
				fieldCommentInfo.namespaces = commentInfo.namespaces;
				fieldCommentInfo.name = commentInfo.name + "::" + I->getDeclName().getAsString();
				fieldCommentInfo.fullName = commentInfo.fullName + "::" + I->getDeclName().getAsString();

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

		if (fullNS.size() != curCommentInfo.namespaces.size())
			continue;

		bool matches = true;
		for (int j = 0; j < (int)curCommentInfo.namespaces.size(); j++)
		{
			if (fullNS[j] != curCommentInfo.namespaces[j])
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

			if (copydocNS.size() != curCommentInfo.namespaces.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)curCommentInfo.namespaces.size(); j++)
			{
				if (copydocNS[j] != curCommentInfo.namespaces[j])
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
		if (!finalCommentInfo.isFunction)
		{
			outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << value << "\".\n";
			return false;
		}

		SmallVector<StringRef, 8> paramSplits;
		params.split(paramSplits, ",", -1, false);

		for (int i = 0; i < (int)paramSplits.size(); i++)
			paramSplits[i] = paramSplits[i].trim();

		int overloadMatch = -1;
		for (int i = 0; i < (int)finalCommentInfo.overloads.size(); i++)
		{
			if (paramSplits.size() != finalCommentInfo.overloads[i].params.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)paramSplits.size(); j++)
			{
				if (paramSplits[j] != finalCommentInfo.overloads[i].params[j])
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

		outputComment = finalCommentInfo.overloads[overloadMatch].comment;
		return true;
	}

	if (finalCommentInfo.isFunction)
		outputComment = finalCommentInfo.overloads[0].comment;
	else
		outputComment = finalCommentInfo.comment;

	return true;
}

void CommentParser::ResolveCopydocComments(CommentEntry& comment, const std::string& parentType, const SmallVector<std::string, 4>& currentNamespace)
{
	StringRef copydocArg;
	for (auto& entry : comment.brief)
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

void CommentParser::EnsureValidParameterReferenceComments(const std::vector<VariableInformation>& paramInfos, CommentText& comment)
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
	for (auto& entry : comment.brief)
		EnsureValidParameterReferenceComments(paramInfos, entry);

	for (auto& entry : comment.params)
	{
		for(auto& textEntry : entry.comments)
			EnsureValidParameterReferenceComments(paramInfos, textEntry);
	}

	for (auto& entry : comment.returns)
		EnsureValidParameterReferenceComments(paramInfos, entry);
}

void CommentParser::ClearParameterReferenceComments(CommentEntry& comment)
{
	EnsureValidParameterReferenceComments({}, comment);
}

std::string CommentParser::GenerateXMLCommentText(const CommentText& commentTextEntry)
{
	uint32_t idx = 0;
	std::stringstream output;

	for (auto& entry : commentTextEntry.Text)
	{
		for (auto& refEntry : commentTextEntry.ParameterReferences)
		{
			if (refEntry.PositionInText == idx)
			{
				output << "<paramref name=\"" << escapeXML(refEntry.Name) << "\"/>";
				idx += refEntry.Name.size();
			}
		}

		for (auto& refEntry : commentTextEntry.DeclarationReferences)
		{
			if (refEntry.PositionInText == idx)
			{
				output << "<see cref=\"" << escapeXML(refEntry.Name) << "\"/>";
				idx += refEntry.Name.size();
			}
		}

		switch (entry)
		{
		case '&':  output << "&amp;";         break;
		case '\"': output << "&quot;";        break;
		case '\'': output << "&apos;";        break;
		case '<':  output << "&lt;";          break;
		case '>':  output << "&gt;";          break;
		default:   output << entry;           break;
		}

		idx++;
	}

	return output.str();
}
std::string CommentParser::GenerateXMLCommentText(const SmallVector<CommentText, 2>& input)
{
	std::stringstream output;
	for (auto I = input.begin(); I != input.end(); ++I)
	{
		if (I != input.begin())
			output << "\n";

		std::string text = GenerateXMLCommentText(*I);
		output << text;
	}

	return output.str();
}

std::string CommentParser::GenerateXMLComments(const CommentEntry& commentEntry, const std::string& indent)
{
	std::stringstream output;

	auto wordWrap = [](const std::string& input, const std::string& linePrefix, int columnLength = 124)
	{
		int prefixLength = (int)linePrefix.length();
		int inputLength = (int)input.length();

		if ((inputLength + prefixLength) <= columnLength)
			return linePrefix + input + "\n";

		StringRef inputRef(input.data(), input.length());
		std::stringstream wordWrapped;

		int lineLength = columnLength - prefixLength;
		int curIdx = 0;
		while (curIdx < inputLength)
		{
			int remainingLength = inputLength - curIdx;
			if (remainingLength <= lineLength)
			{
				StringRef lineRef = inputRef.substr(curIdx, remainingLength);
				wordWrapped << linePrefix << lineRef.str() << std::endl;
				break;
			}
			else
			{
				int lastSpace = inputRef.find_last_of(' ', curIdx + lineLength);
				if (lastSpace == -1 || lastSpace <= curIdx) // Need to break word
				{
					StringRef lineRef = inputRef.substr(curIdx, lineLength);

					wordWrapped << linePrefix << lineRef.str() << std::endl;
					curIdx += lineLength;
				}
				else
				{
					int length = lastSpace - curIdx + 1;
					StringRef lineRef = inputRef.substr(curIdx, length);

					wordWrapped << linePrefix << lineRef.str() << std::endl;
					curIdx += length;
				}
			}
		}

		return wordWrapped.str();
	};

	auto printParagraphs = [&output, &indent, &wordWrap](const std::string& head, const std::string& tail, const SmallVector<CommentText, 2>& input)
	{
		bool multiline = false;
		if (input.size() > 1)
			multiline = true;
		else
		{
			int refLength = 0;
			for (auto& entry : input[0].ParameterReferences)
				refLength += sizeof("<paramref name=\"\"/>") + entry.Name.size();

			for (auto& entry : input[0].DeclarationReferences)
				refLength += sizeof("<see cref=\"\"/>") + entry.Name.size();

			int lineLength = head.length() + tail.length() + indent.size() + 4 + input[0].Text.size() + refLength;
			if (lineLength >= 124)
				multiline = true;
		}

		if (multiline)
		{
			output << indent << "/// " << head << "\n";
			for (auto I = input.begin(); I != input.end(); ++I)
			{
				if (I != input.begin())
					output << indent << "///\n";

				std::string text = GenerateXMLCommentText(*I);
				output << wordWrap(text, indent + "/// ");
			}
			output << indent << "/// " << tail << "\n";
		}
		else
		{
			std::string text = GenerateXMLCommentText(input[0]);
			output << indent << "/// " << head << text << tail << "\n";
		}
	};

	if (!commentEntry.brief.empty())
		printParagraphs("<summary>", "</summary>", commentEntry.brief);
	else
	{
		if (!commentEntry.params.empty() || !commentEntry.returns.empty())
			output << indent << "/// <summary></summary>" << std::endl;
	}

	for (auto& entry : commentEntry.params)
	{
		if (entry.comments.empty())
			continue;

		printParagraphs("<param name=\"" + entry.Name + "\">", "</param>", entry.comments);
	}

	if (!commentEntry.returns.empty())
		printParagraphs("<returns>", "</returns>", commentEntry.returns);

	return output.str();
}
