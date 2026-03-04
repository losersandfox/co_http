#ifndef HTTP_HPP 
#define HTTP_HPP
#include <charconv>
#include <string>
#include <map>
#include <unordered_map>
#include "BytesBuffer.hpp"
#include "co_async/Stream.hpp"
#include <string_view>

using StringMap = std::map<std::string, std::string>;

namespace http{

    //下面是http的请求和响应的报文相关的内偶然能够

    struct Http11HeaderParse{
        BytesBuffer m_header;       // GET HTTP/1.1  {Host等键值对}
        std::string m_header_line;  // GET HTTP/1.1
        StringMap   m_header_keys;  // {键值对}
        std::string m_body;         // 不小心超量阅读的文本
        bool m_header_is_finished = false;

        [[nodiscard]] bool header_finished(){
            return m_header_is_finished;
        }

        void reset_state() {
            m_header.clear();
            m_header_line.clear();
            m_header_keys.clear();
            m_body.clear();
            m_header_is_finished = 0;
        }

        void _extract_headers(){
            std::string_view header = m_header;
            size_t pos = header.find("\r\n");
            m_header_line = header.substr(0, pos);
            while(pos != std::string::npos){
                //绕过\r\n
                pos += 2;
                size_t p_end =  header.find("\r\n", pos);
                size_t line_len = 0;
                if(p_end != std::string::npos){
                    line_len = p_end - pos;
                }
                else{
                    line_len = header.size() - pos;
                }
                //获取本行的字符串
                std::string line_string = std::string(header.substr(pos, line_len));
                int mid_index = line_string.find(":");
                if(mid_index != std::string::npos){
                    std::string key = line_string.substr(0, mid_index);
                    std::string value = line_string.substr(mid_index + 2);

                    for (char &c: key) {
                        if ('A' <= c && c <= 'Z') {
                            c += 'a' - 'A';
                        }
                    }
                    m_header_keys.insert_or_assign(std::move(key), value);
                }
                pos = p_end;
            }
        }
    
        void push_chunk(BytesConstView &chunk){
            if(!m_header_is_finished){

                m_header.append(chunk);
                std::string_view header = m_header;
                size_t header_len = header.find("\r\n\r\n");
                //判断头部是否结束
                if(m_header.size() != std::string::npos){
                    m_header_is_finished = true;
                    //不小心多的正文留下
                    m_body = header.substr(header_len + 4);
                    m_header.resize(header_len);
                    _extract_headers();
                }
            }
            else{
               m_body.append(chunk); 
            }
        }
        std::string &headline() {
            return m_header_line;
        }

        StringMap &headers() {
            return m_header_keys;
        }

        BytesBuffer &headers_raw() {
            return m_header;
        }

        std::string &body(){
            return m_body;
        }
    };

    template<class HeaderParser = Http11HeaderParse>
    struct HttpBaseParser{
        HeaderParser m_header_parser;
        size_t       m_body_length;
        bool         m_body_finished;

        [[nodiscard]] bool body_finished(){
            return !m_body_finished;
        }

        std::string &body(){
            return m_header_parser.m_body;
        }

        size_t _extract_body_length(){
            auto &headers = m_header_parser.headers();
            auto it = headers.find("content-length");
            if(it == headers.end()){
                return 0;                
            }
            size_t result;
            auto res_str = it->second;
            std::from_chars(res_str.data(), res_str.data() + res_str.size(), result);
            return result;
        }

        void push_chunk(BytesConstView chunk){
            if(!m_header_parser.header_finished()){
                m_header_parser.push_chunk(chunk);
                if(m_header_parser.header_finished()){
                    m_body_length = _extract_body_length();
                    if(body().size() >= m_body_length){
                        m_body_finished = true;
                        body().resize(m_body_length);
                    }
                }
            }
            else{
                body().append(chunk);
                if(body().size() >= m_body_length){
                    m_body_finished = true;
                    body().resize(m_body_length);
                }
            }
        }

        std::string &headline() {
            return m_header_parser.m_header_line;
        }

        StringMap &headers() {
            return m_header_parser.m_header_keys;
        }

        std::string &headers_raw() {
            return m_header_parser.m_header;
        }
    };

    enum HttpMethod{
        UNKNOWN = -1,
        GET,
        POST,
        PUT,
        DELETE,
        HEAD,
        OPTIONS,
        PATCH,
        TRACE,
        CONNECT,
    };

    template<class HeaderParser = Http11HeaderParse>
    struct HttpRequestParse : public HttpBaseParser<HeaderParser>{
        std::string method(){
            //"GET / HTTP/1.1"
            auto &headline = this->m_header_parser.headline();
            size_t pos = headline.find(" ");
            if(pos != std::string::npos){
                return headline.substr(0, pos);
            }
            return "GET";    
        }
        std::string url(){
            auto &headline = this->m_header_parser.headline();
            size_t pos1 = headline.find(" ");
            if(pos1 == std::string::npos){
                throw "Not find url";
            }
            size_t pos2 = headline.find(" ", pos1 + 1);
            if(pos2 == std::string::npos){
                throw "Not find url";
            } 
            return headline.substr(pos1, pos2 - pos1);
        }
    };

    template<class HeaderParser = Http11HeaderParse>
    struct HttpResponseParser : public HttpBaseParser<HeaderParser>{
        //"HTTP/1.1 200 OK"
        int status(){
            auto &headline = this->m_header_parser.headline();
            size_t pos1 = headline.find(" ");
            if(pos1 == std::string::npos){
                return -1;
            }
            size_t pos2 = headline.find(" ", pos1 + 1);
            if(pos2 == std::string::npos){
                return -1;
            }
            return std::stoi(headline.substr(pos1, pos2 - pos1));
        }
    };

    struct Http11WriteHeader {
        BytesBuffer m_buffer;

        void reset_state() {
            m_buffer.clear();
        }

        BytesBuffer &buffer() {
            return m_buffer;
        }

        void begin_header(std::string_view first, std::string_view second,
                        std::string_view third) {
            m_buffer.append(first);
            m_buffer.append_literial(" ");
            m_buffer.append(second);
            m_buffer.append_literial(" ");
            m_buffer.append(third);
        }

        void write_header(std::string_view key, std::string_view value) {
            m_buffer.append_literial("\r\n");
            m_buffer.append(key);
            m_buffer.append_literial(": ");
            m_buffer.append(value);
        }

        void end_header() {
            m_buffer.append_literial("\r\n\r\n");
        }
    };

    template <class HeaderWriter = Http11WriteHeader>
    struct HttpBaseWriter {
        HeaderWriter m_header_writer;

        void _begin_header(std::string_view first, std::string_view second,
                        std::string_view third) {
            m_header_writer.begin_header(first, second, third);
        }

        void reset_state() {
            m_header_writer.reset_state();
        }

        BytesBuffer &buffer() {
            return m_header_writer.buffer();
        }


        void write_header(std::string_view key, std::string value) {
            m_header_writer.write_header(key, value);
        }

        void write_header(std::string_view key, int value){
            m_header_writer.write_header(key, std::to_string(value));
        }

        void end_header() {
            m_header_writer.end_header();
        }

        void write_body(std::string_view body) {
            m_header_writer.buffer().append(body);
        }
    };

    template <class HeaderWriter = Http11WriteHeader>
    struct HttpRequestWriter : HttpBaseWriter<HeaderWriter> {
        void begin_header(std::string_view method, std::string_view url) {
            this->_begin_header(method, url, "HTTP/1.1");
        }
    };
    const std::unordered_map<int, std::string> http_status = {
        // 1xx: Informational (信息性状态码)
        {100, "Continue"},
        {101, "Switching Protocols"},
        {102, "Processing"},
        {103, "Early Hints"},

        // 2xx: Success (成功状态码)
        {200, "OK"},
        {201, "Created"},
        {202, "Accepted"},
        {203, "Non-Authoritative Information"},
        {204, "No Content"},
        {205, "Reset Content"},
        {206, "Partial Content"},
        {207, "Multi-Status"},
        {208, "Already Reported"},
        {226, "IM Used"},

        // 3xx: Redirection (重定向状态码)
        {300, "Multiple Choices"},
        {301, "Moved Permanently"},
        {302, "Found"},
        {303, "See Other"},
        {304, "Not Modified"},
        {305, "Use Proxy"},
        {307, "Temporary Redirect"},
        {308, "Permanent Redirect"},

        // 4xx: Client Error (客户端错误状态码)
        {400, "Bad Request"},
        {401, "Unauthorized"},
        {402, "Payment Required"},
        {403, "Forbidden"},
        {404, "Not Found"},
        {405, "Method Not Allowed"},
        {406, "Not Acceptable"},
        {407, "Proxy Authentication Required"},
        {408, "Request Timeout"},
        {409, "Conflict"},
        {410, "Gone"},
        {411, "Length Required"},
        {412, "Precondition Failed"},
        {413, "Payload Too Large"},
        {414, "URI Too Long"},
        {415, "Unsupported Media Type"},
        {416, "Range Not Satisfiable"},
        {417, "Expectation Failed"},
        {418, "I'm a teapot"}, // RFC 2324
        {421, "Misdirected Request"},
        {422, "Unprocessable Entity"},
        {423, "Locked"},
        {424, "Failed Dependency"},
        {425, "Too Early"},
        {426, "Upgrade Required"},
        {428, "Precondition Required"},
        {429, "Too Many Requests"},
        {431, "Request Header Fields Too Large"},
        {451, "Unavailable For Legal Reasons"},

        // 5xx: Server Error (服务器错误状态码)
        {500, "Internal Server Error"},
        {501, "Not Implemented"},
        {502, "Bad Gateway"},
        {503, "Service Unavailable"},
        {504, "Gateway Timeout"},
        {505, "HTTP Version Not Supported"},
        {506, "Variant Also Negotiates"},
        {507, "Insufficient Storage"},
        {508, "Loop Detected"},
        {510, "Not Extended"},
        {511, "Network Authentication Required"}
    };
    template <class HeaderWriter = Http11WriteHeader>
    struct HttpResponseWriter : HttpBaseWriter<HeaderWriter> {
        void begin_header(int status) {
            this->_begin_header("HTTP/1.1", std::to_string(status), http_status.at(status));
        }
    };

    struct HTTPProtocol{
        co_async::FileStream m_stream;

        explicit HTTPProtocol(co_async::FileStream stream) : m_stream(std::move(stream)) {}
        HTTPProtocol(HTTPProtocol&& that) = delete;
        HTTPProtocol& operator=(HTTPProtocol&& that) = delete;

        co_async::Task<BytesConstView> requestRead(){
                co_async::AsyncFile &sock = m_stream.mFile;
                char buf[4096];
                ssize_t n = co_await m_stream.read(buf);
                if(n < 0){
                    spdlog::info("读取出错");
                    co_return {};
                }
                BytesConstView view{buf, static_cast<size_t>(n)};
                // 这里可以继续解析请求，生成响应等
                
                co_return view;
        }

        co_async::Task<void> responseWrite(std::string_view response) {
            co_await m_stream.write(response);
        }
    };
}

#endif