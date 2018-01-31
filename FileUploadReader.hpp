#pragma once
#include <boost/system/error_code.hpp>
#include <beast/http/basic_parser_v1.hpp>
#include <string>
#include <cstdlib>

namespace WebSocketServer
{
	enum class UploadErrorCode
	{
		kNoError,
		kInvalidPath,
		kInvalidContentType,
		kNoContentLen

	};

	class UploadErrorCategory : public boost::system::error_category
	{
	public:
		const char* name() const noexcept override
		{
			return "Upload error";
		}

		std::string message(int ev) const override
		{
			switch (static_cast<UploadErrorCode>(ev))
			{
			default:
			case UploadErrorCode::kNoError:
				return "No error";
			case UploadErrorCode::kInvalidPath:
				return "Invalid path";
			case UploadErrorCode::kInvalidContentType:
				return "Invalid content type";
			case UploadErrorCode::kNoContentLen:
				return "No content length";
			}
		}
	};

	inline const UploadErrorCategory& GetUploadErrorCategory()
	{
		static const UploadErrorCategory category;
		return category;
	}

	struct FileUploadReader
	{
		using value_type = std::string;

		class reader
		{
			std::string& body_;
			UploadErrorCode error_code_;
		public:
			template<bool isRequest, typename Fields>
			reader(beast::http::message<isRequest, FileUploadReader, Fields>& msg) noexcept :
				body_(msg.body)
			{
				if (msg.url != "/upload")
				{
					error_code_ = UploadErrorCode::kInvalidPath;
					return;
				}

				const auto content_type = msg.fields.find("Content-Type");
				if (content_type == msg.fields.end() ||
					!beast::detail::ci_equal(content_type->second, "application/octet-stream"))
				{
					error_code_ = UploadErrorCode::kInvalidContentType;
					return;
				}

				// NOTE: The content length should have already been parsed by the parser
				// but it can't be accessed so it needs to be done again
				const auto content_length = msg.fields.find("Content-Length");
				if (content_length != msg.fields.end())
				{
					unsigned long len = std::strtoul(content_length->second.c_str(), nullptr, 10);
					
				}

				// TODO: Respond to Expect headers
				//const auto expect_header = m.fields.find("expect");
				//if (expect_header != m.fields.cend())
				//{
				//	if (beast::detail::ci_equal(expect_header->second, "100-continue"))
				//	{
				//		// Respond with 100 (Continue) to accept upload
				//	}
				//	else
				//	{
				//		// Respond to unrecognized expectation with 417 (Expectation Failed)
				//	}
				//}


			}

			void init(boost::system::error_code& ec) noexcept
			{
				if (error_code_ != UploadErrorCode::kNoError)
					ec = boost::system::error_code(static_cast<int>(error_code_), GetUploadErrorCategory());
			}

			void write(const void* data, std::size_t size, boost::system::error_code&) noexcept
			{

			}
		};
	};
}
