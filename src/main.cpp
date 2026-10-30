#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket INVALID_SOCKET_VALUE = INVALID_SOCKET;
static void closeSocket(Socket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
static constexpr Socket INVALID_SOCKET_VALUE = -1;
static void closeSocket(Socket socket) { close(socket); }
#endif

namespace {

struct Account {
    int id{};
    std::string name;
    double balance{};
    std::string createdAt;
};

struct Transaction {
    int id{};
    int accountId{};
    std::string type;
    double amount{};
    std::string timestamp;
    std::string description;
    int relatedAccountId{};
};

std::mutex dataMutex;
std::vector<Account> accounts;
std::vector<Transaction> transactions;
std::filesystem::path appDirectory;
int nextAccountId = 1;
int nextTransactionId = 1;
std::atomic<bool> running{true};
constexpr int PORT = 8080;
constexpr std::size_t MAX_REQUEST_SIZE = 1024 * 1024;

bool hasPublicFiles(const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::is_regular_file(
        directory / "public" / "index.html", error);
}

std::filesystem::path findAppDirectory(const char* executable) {
    std::vector<std::filesystem::path> candidates;
    const auto addAncestors = [&candidates](
                                  std::filesystem::path directory) {
        std::error_code error;
        directory = std::filesystem::weakly_canonical(directory, error);
        if (error) return;
        for (int depth = 0; depth < 8; ++depth) {
            candidates.push_back(directory);
            const auto parent = directory.parent_path();
            if (parent == directory) break;
            directory = parent;
        }
    };

    addAncestors(std::filesystem::current_path());
    const auto executablePath = std::filesystem::absolute(executable);
    addAncestors(executablePath.parent_path());

    for (const auto& candidate : candidates) {
        std::error_code error;
        if (hasPublicFiles(candidate) &&
            std::filesystem::is_regular_file(candidate / "CMakeLists.txt",
                                             error)) {
            return candidate;
        }
    }
    for (const auto& candidate : candidates) {
        if (hasPublicFiles(candidate)) return candidate;
    }
    throw std::runtime_error(
        "Could not find the public web files. Build and start the app with run.bat.");
}

std::string now() {
    const auto time = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream result;
    result << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
    return result.str();
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream result;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': result << "\\\""; break;
        case '\\': result << "\\\\"; break;
        case '\b': result << "\\b"; break;
        case '\f': result << "\\f"; break;
        case '\n': result << "\\n"; break;
        case '\r': result << "\\r"; break;
        case '\t': result << "\\t"; break;
        default:
            if (ch < 0x20) {
                result << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(ch)
                       << std::dec << std::setfill(' ');
            } else {
                result << static_cast<char>(ch);
            }
        }
    }
    return result.str();
}

std::string jsonString(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string accountJson(const Account& account) {
    std::ostringstream json;
    json << "{\"id\":" << account.id << ",\"name\":"
         << jsonString(account.name) << ",\"balance\":" << std::fixed
         << std::setprecision(2) << account.balance << ",\"createdAt\":"
         << jsonString(account.createdAt) << "}";
    return json.str();
}

std::string transactionJson(const Transaction& transaction) {
    std::ostringstream json;
    json << "{\"id\":" << transaction.id << ",\"accountId\":"
         << transaction.accountId << ",\"type\":"
         << jsonString(transaction.type) << ",\"amount\":" << std::fixed
         << std::setprecision(2) << transaction.amount << ",\"timestamp\":"
         << jsonString(transaction.timestamp) << ",\"description\":"
         << jsonString(transaction.description) << ",\"relatedAccountId\":"
         << transaction.relatedAccountId << "}";
    return json.str();
}

void saveData() {
    const auto dataDirectory = appDirectory / "data";
    std::filesystem::create_directories(dataDirectory);
    const auto dataPath = dataDirectory / "bank-data.txt";
    const auto temporaryPath = dataDirectory / "bank-data.txt.tmp";
    std::ofstream file(temporaryPath, std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not write " + temporaryPath.string());
    }
    file << std::setprecision(17);
    for (const auto& account : accounts) {
        file << "A " << account.id << ' ' << std::quoted(account.name) << ' '
             << account.balance << ' ' << std::quoted(account.createdAt)
             << '\n';
    }
    for (const auto& transaction : transactions) {
        file << "T " << transaction.id << ' ' << transaction.accountId << ' '
             << std::quoted(transaction.type) << ' ' << transaction.amount
             << ' ' << std::quoted(transaction.timestamp) << ' '
             << std::quoted(transaction.description) << ' '
             << transaction.relatedAccountId << '\n';
    }
    file.close();
    if (!file) {
        throw std::runtime_error("Could not finish saving bank data");
    }
    std::error_code error;
    std::filesystem::rename(temporaryPath, dataPath, error);
    if (error) {
        std::filesystem::remove(dataPath, error);
        error.clear();
        std::filesystem::rename(temporaryPath, dataPath, error);
    }
    if (error) {
        throw std::runtime_error("Could not replace " + dataPath.string() +
                                 ": " + error.message());
    }
}

void loadData() {
    const auto dataDirectory = appDirectory / "data";
    const auto dataPath = dataDirectory / "bank-data.txt";
    std::filesystem::create_directories(dataDirectory);
    std::ifstream file(dataPath);
    if (!file) {
        if (std::filesystem::exists(dataPath)) {
            throw std::runtime_error("Could not read " + dataPath.string());
        }
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream row(line);
        char kind{};
        row >> kind;
        if (kind == 'A') {
            Account account;
            if (row >> account.id >> std::quoted(account.name) >>
                    account.balance >> std::quoted(account.createdAt)) {
                accounts.push_back(account);
                nextAccountId = std::max(nextAccountId, account.id + 1);
            }
        } else if (kind == 'T') {
            Transaction transaction;
            if (row >> transaction.id >> transaction.accountId >>
                    std::quoted(transaction.type) >> transaction.amount >>
                    std::quoted(transaction.timestamp) >>
                    std::quoted(transaction.description) >>
                    transaction.relatedAccountId) {
                transactions.push_back(transaction);
                nextTransactionId =
                    std::max(nextTransactionId, transaction.id + 1);
            }
        }
    }
    if (!file.eof()) {
        throw std::runtime_error("Could not read " + dataPath.string());
    }
}

std::string urlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            result.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const long decoded = std::strtol(hex.c_str(), &end, 16);
            if (end == hex.c_str() + 2) {
                result.push_back(static_cast<char>(decoded));
                i += 2;
            } else {
                result.push_back(value[i]);
            }
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::map<std::string, std::string> parseForm(const std::string& body) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string::npos
                                                ? std::string::npos
                                                : end - start);
        const auto equals = part.find('=');
        if (equals != std::string::npos) {
            fields[urlDecode(part.substr(0, equals))] =
                urlDecode(part.substr(equals + 1));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

bool parsePositiveInt(const std::string& value, int& result) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size() || parsed <= 0) return false;
        result = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseAmount(const std::string& value, double& result) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(parsed) || parsed < 0 ||
            parsed > 1000000000.0) return false;
        result = std::round(parsed * 100.0) / 100.0;
        return result >= 0;
    } catch (const std::exception&) {
        return false;
    }
}

Account* findAccount(int id) {
    const auto found = std::find_if(accounts.begin(), accounts.end(),
                                    [id](const Account& account) {
                                        return account.id == id;
                                    });
    return found == accounts.end() ? nullptr : &*found;
}

struct Response {
    int status{200};
    std::string contentType{"application/json; charset=utf-8"};
    std::string body;
};

Response errorResponse(int status, const std::string& message) {
    return {status, "application/json; charset=utf-8",
            "{\"error\":" + jsonString(message) + "}"};
}

std::string accountsJson() {
    std::ostringstream json;
    json << '[';
    for (std::size_t i = 0; i < accounts.size(); ++i) {
        if (i) json << ',';
        json << accountJson(accounts[i]);
    }
    json << ']';
    return json.str();
}

std::string transactionsJson(int accountId = 0) {
    std::ostringstream json;
    json << '[';
    bool first = true;
    for (auto it = transactions.rbegin(); it != transactions.rend(); ++it) {
        if (accountId && it->accountId != accountId) continue;
        if (!first) json << ',';
        json << transactionJson(*it);
        first = false;
    }
    json << ']';
    return json.str();
}

Response handleApi(const std::string& method, const std::string& path,
                   const std::string& body) {
    std::lock_guard<std::mutex> lock(dataMutex);
    if (method == "GET" && path == "/api/accounts") {
        return {200, "application/json; charset=utf-8", accountsJson()};
    }
    if (method == "GET" && path.rfind("/api/transactions", 0) == 0) {
        int accountId = 0;
        const auto query = path.find("accountId=");
        if (query != std::string::npos) {
            const auto value = path.substr(query + 10);
            if (!parsePositiveInt(value, accountId)) {
                return errorResponse(400, "Invalid account ID.");
            }
        }
        return {200, "application/json; charset=utf-8",
                transactionsJson(accountId)};
    }
    if (method != "POST") return errorResponse(404, "Endpoint not found.");

    const auto fields = parseForm(body);
    if (path == "/api/accounts") {
        const auto nameField = fields.find("name");
        const auto balanceField = fields.find("openingBalance");
        if (nameField == fields.end() || balanceField == fields.end()) {
            return errorResponse(400, "Name and opening balance are required.");
        }
        const std::string name = nameField->second;
        if (name.empty() || name.size() > 60 ||
            std::all_of(name.begin(), name.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
            })) {
            return errorResponse(400, "Enter a name between 1 and 60 characters.");
        }
        double openingBalance = 0;
        if (!parseAmount(balanceField->second, openingBalance)) {
            return errorResponse(400, "Opening balance must be zero or a positive amount.");
        }
        Account account{nextAccountId++, name, openingBalance, now()};
        accounts.push_back(account);
        if (openingBalance > 0) {
            transactions.push_back({nextTransactionId++, account.id, "deposit",
                                    openingBalance, account.createdAt,
                                    "Opening balance", 0});
        }
        try {
            saveData();
        } catch (const std::exception& exception) {
            accounts.pop_back();
            if (openingBalance > 0) transactions.pop_back();
            --nextAccountId;
            return errorResponse(500, exception.what());
        }
        return {201, "application/json; charset=utf-8", accountJson(account)};
    }

    if (path == "/api/transactions") {
        const auto kindField = fields.find("type");
        const auto accountField = fields.find("accountId");
        const auto amountField = fields.find("amount");
        if (kindField == fields.end() || accountField == fields.end() ||
            amountField == fields.end()) {
            return errorResponse(400, "Transaction type, account, and amount are required.");
        }
        int accountId = 0;
        if (!parsePositiveInt(accountField->second, accountId)) {
            return errorResponse(400, "Choose a valid account.");
        }
        Account* account = findAccount(accountId);
        if (!account) return errorResponse(404, "Account not found.");
        double amount = 0;
        if (!parseAmount(amountField->second, amount) || amount <= 0) {
            return errorResponse(400, "Enter an amount greater than zero.");
        }
        const std::string type = kindField->second;
        Account* related = nullptr;
        if (type == "transfer") {
            const auto recipientField = fields.find("recipientId");
            int recipientId = 0;
            if (recipientField == fields.end() ||
                !parsePositiveInt(recipientField->second, recipientId)) {
                return errorResponse(400, "Choose a valid destination account.");
            }
            related = findAccount(recipientId);
            if (!related) return errorResponse(404, "Destination account not found.");
            if (related->id == account->id) {
                return errorResponse(400, "Choose a different destination account.");
            }
        } else if (type != "deposit" && type != "withdraw") {
            return errorResponse(400, "Choose deposit, withdrawal, or transfer.");
        }
        if (type != "deposit" && account->balance + 0.000001 < amount) {
            return errorResponse(400, "Insufficient funds for this transaction.");
        }
        const double originalBalance = account->balance;
        const double relatedBalance = related ? related->balance : 0;
        const auto originalTransactionCount = transactions.size();
        const int originalNextTransactionId = nextTransactionId;
        std::string description;
        const auto noteField = fields.find("note");
        if (noteField != fields.end()) description = noteField->second;
        if (description.size() > 80) {
            return errorResponse(400, "Notes must be 80 characters or fewer.");
        }
        if (type == "deposit") {
            account->balance += amount;
            if (description.empty()) description = "Cash deposit";
            transactions.push_back({nextTransactionId++, account->id, type,
                                    amount, now(), description, 0});
        } else if (type == "withdraw") {
            account->balance = std::round((account->balance - amount) * 100.0) / 100.0;
            if (description.empty()) description = "Cash withdrawal";
            transactions.push_back({nextTransactionId++, account->id, type,
                                    amount, now(), description, 0});
        } else {
            account->balance = std::round((account->balance - amount) * 100.0) / 100.0;
            related->balance += amount;
            const std::string note = description.empty()
                ? std::string{}
                : ": " + description;
            transactions.push_back({nextTransactionId++, account->id,
                                    "transfer_out", amount, now(),
                                    "Transfer to " + related->name + note,
                                    related->id});
            transactions.push_back({nextTransactionId++, related->id,
                                    "transfer_in", amount, now(),
                                    "Transfer from " + account->name + note,
                                    account->id});
        }
        try {
            saveData();
        } catch (const std::exception& exception) {
            account->balance = originalBalance;
            if (related) related->balance = relatedBalance;
            transactions.resize(originalTransactionCount);
            nextTransactionId = originalNextTransactionId;
            return errorResponse(500, exception.what());
        }
        return {201, "application/json; charset=utf-8",
                transactionsJson(accountId)};
    }
    return errorResponse(404, "Endpoint not found.");
}

std::string statusText(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

bool sendAll(Socket socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int result = send(socket, data.data() + sent,
                                static_cast<int>(data.size() - sent), 0);
#else
        const auto result = send(socket, data.data() + sent,
                                 data.size() - sent, 0);
#endif
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void sendResponse(Socket socket, const Response& response) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << response.status << ' '
            << statusText(response.status) << "\r\n"
            << "Content-Type: " << response.contentType << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n\r\n";
    sendAll(socket, headers.str() + response.body);
}

Response serveStatic(const std::string& path) {
    std::string filename;
    std::string type;
    if (path == "/" || path == "/index.html") {
        filename = (appDirectory / "public" / "index.html").string();
        type = "text/html; charset=utf-8";
    } else if (path == "/styles.css") {
        filename = (appDirectory / "public" / "styles.css").string();
        type = "text/css; charset=utf-8";
    } else if (path == "/app.js") {
        filename = (appDirectory / "public" / "app.js").string();
        type = "text/javascript; charset=utf-8";
    } else {
        return {404, "text/plain; charset=utf-8", "Not found"};
    }
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return {500, "text/plain; charset=utf-8",
                "Could not read " + filename + ". Start the app from its project folder."};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (!file.eof() && file.fail()) {
        return {500, "text/plain; charset=utf-8",
                "Could not finish reading " + filename};
    }
    return {200, type, contents.str()};
}

void handleClient(Socket client) {
    std::string request;
    request.reserve(4096);
    char buffer[8192];
    std::size_t headerEnd = std::string::npos;
    std::size_t expectedSize = 0;
    while (request.size() < MAX_REQUEST_SIZE) {
#ifdef _WIN32
        const int received = recv(client, buffer, sizeof(buffer), 0);
#else
        const auto received = recv(client, buffer, sizeof(buffer), 0);
#endif
        if (received <= 0) break;
        request.append(buffer, static_cast<std::size_t>(received));
        headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            const auto lengthHeader = request.find("Content-Length:");
            if (lengthHeader != std::string::npos && lengthHeader < headerEnd) {
                const auto valueStart = lengthHeader + 15;
                const auto valueEnd = request.find("\r\n", valueStart);
                try {
                    expectedSize = static_cast<std::size_t>(
                        std::stoul(request.substr(valueStart, valueEnd - valueStart)));
                } catch (const std::exception&) {
                    sendResponse(client, errorResponse(400, "Invalid request."));
                    closeSocket(client);
                    return;
                }
            }
            const std::size_t bodyStart = headerEnd + 4;
            if (expectedSize > MAX_REQUEST_SIZE ||
                request.size() >= bodyStart + expectedSize) break;
        }
    }
    if (headerEnd == std::string::npos) {
        sendResponse(client, errorResponse(400, "Invalid request."));
        closeSocket(client);
        return;
    }
    const auto firstLineEnd = request.find("\r\n");
    std::istringstream firstLine(request.substr(0, firstLineEnd));
    std::string method, path, version;
    firstLine >> method >> path >> version;
    if (method.empty() || path.empty() || version.rfind("HTTP/", 0) != 0) {
        sendResponse(client, errorResponse(400, "Invalid request."));
        closeSocket(client);
        return;
    }
    if (path.rfind("/api/", 0) == 0) {
        const std::size_t bodyStart = headerEnd + 4;
        const std::string body = request.substr(bodyStart, expectedSize);
        sendResponse(client, handleApi(method, path, body));
    } else if (method == "GET") {
        sendResponse(client, serveStatic(path));
    } else {
        sendResponse(client, errorResponse(404, "Endpoint not found."));
    }
    closeSocket(client);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 1 || argv[0] == nullptr) {
        std::cerr << "Could not determine the application location.\n";
        return 1;
    }
#ifdef _WIN32
    WSADATA winsockData{};
    if (WSAStartup(MAKEWORD(2, 2), &winsockData) != 0) {
        std::cerr << "Could not initialize Windows networking.\n";
        return 1;
    }
#endif
    try {
        appDirectory = findAppDirectory(argv[0]);
        loadData();
    } catch (const std::exception& exception) {
        std::cerr << "Could not start Bank Manager: " << exception.what() << '\n';
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    Socket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET_VALUE) {
        std::cerr << "Could not create the web server socket.\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               reinterpret_cast<const char*>(&reuse),
#else
               &reuse,
#endif
               sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, SOMAXCONN) != 0) {
        std::cerr << "Could not listen on http://127.0.0.1:" << PORT
                  << ". Is the port already in use?\n";
        closeSocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::cout << "Bank Manager is running at http://127.0.0.1:" << PORT
              << "\nProject files: " << appDirectory.string()
              << "\nData is stored in "
              << (appDirectory / "data" / "bank-data.txt").string()
              << "\nPress Ctrl+C to stop.\n";
    while (running) {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int clientAddressSize = sizeof(clientAddress);
#else
        socklen_t clientAddressSize = sizeof(clientAddress);
#endif
        const Socket client = accept(
            server, reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressSize);
        if (client == INVALID_SOCKET_VALUE) {
            std::cerr << "A client connection could not be accepted.\n";
            continue;
        }
        std::thread(handleClient, client).detach();
    }
    closeSocket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
