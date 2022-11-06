#include "B3DXMLCommentGenerator.h"
#include "B3DGeneratorUtility.h"

std::string XMLCommentGenerator::GenerateXMLCommentParagraph(const CommentParagraph& commentTextEntry)
{
	uint32_t idx = 0;
	std::stringstream output;

	for (auto& entry : commentTextEntry.Text)
	{
		for (auto& refEntry : commentTextEntry.ParameterReferences)
		{
			if (refEntry.PositionInText == idx)
			{
				output << "<paramref name=\"" << GeneratorUtility::EscapeXML(refEntry.Name) << "\"/>";
				idx += refEntry.Name.size();
			}
		}

		for (auto& refEntry : commentTextEntry.DeclarationReferences)
		{
			if (refEntry.PositionInText == idx)
			{
				output << "<see cref=\"" << GeneratorUtility::EscapeXML(refEntry.Name) << "\"/>";
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

std::string XMLCommentGenerator::GenerateXMLCommentParagraph(const SmallVector<CommentParagraph, 2>& input)
{
	std::stringstream output;
	for (auto I = input.begin(); I != input.end(); ++I)
	{
		if (I != input.begin())
			output << "\n";

		std::string text = GenerateXMLCommentParagraph(*I);
		output << text;
	}

	return output.str();
}

std::string XMLCommentGenerator::GenerateXMLComment(const CommentEntry& commentEntry, const std::string& indent)
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

	auto printParagraphs = [&output, &indent, &wordWrap](const std::string& head, const std::string& tail, const SmallVector<CommentParagraph, 2>& input)
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

				std::string text = GenerateXMLCommentParagraph(*I);
				output << wordWrap(text, indent + "/// ");
			}
			output << indent << "/// " << tail << "\n";
		}
		else
		{
			std::string text = GenerateXMLCommentParagraph(input[0]);
			output << indent << "/// " << head << text << tail << "\n";
		}
	};

	if (!commentEntry.Brief.empty())
		printParagraphs("<summary>", "</summary>", commentEntry.Brief);
	else
	{
		if (!commentEntry.ParameterComments.empty() || !commentEntry.ReturnValueComments.empty())
			output << indent << "/// <summary></summary>" << std::endl;
	}

	for (auto& entry : commentEntry.ParameterComments)
	{
		if (entry.Comments.empty())
			continue;

		printParagraphs("<param name=\"" + entry.Name + "\">", "</param>", entry.Comments);
	}

	if (!commentEntry.ReturnValueComments.empty())
		printParagraphs("<returns>", "</returns>", commentEntry.ReturnValueComments);

	return output.str();
}
