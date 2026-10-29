// RAII 与智能指针练习演示：
//   1. 手写 RAII 守卫类：构造获取资源、析构释放资源、禁止拷贝
//   2. unique_ptr + 自定义 deleter 托管 FILE*
//   3. shared_ptr 引用计数、weak_ptr 不延长生命周期
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

class ResourceGuard {
public:
    explicit ResourceGuard(const char* name) : name_(name) {
        std::cout << "[ResourceGuard] acquire " << name_ << '\n';
    }

    ~ResourceGuard() { std::cout << "[ResourceGuard] release " << name_ << '\n'; }

    ResourceGuard(const ResourceGuard&) = delete;
    ResourceGuard& operator=(const ResourceGuard&) = delete;

private:
    const char* name_;
};

struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        std::fclose(file);
        std::cout << "[unique_ptr] FILE* closed by custom deleter\n";
    }
};

void unique_ptr_demo() {
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen("/tmp/raii_demo.txt", "w"));
    if (file == nullptr) {
        throw std::runtime_error("fopen failed");
    }
    std::fputs("managed by unique_ptr\n", file.get());
}  // 作用域结束自动 fclose，即使中途抛异常也不会泄漏

void shared_ptr_demo() {
    auto value = std::make_shared<int>(42);
    std::cout << "[shared_ptr] use_count = " << value.use_count() << '\n';
    {
        std::shared_ptr<int> alias = value;
        std::cout << "[shared_ptr] use_count = " << value.use_count()
                  << "（alias 存在时）\n";
    }
    std::cout << "[shared_ptr] use_count = " << value.use_count()
              << "（alias 析构后）\n";
}

void weak_ptr_demo() {
    auto value = std::make_shared<int>(7);
    std::weak_ptr<int> observer = value;
    if (auto locked = observer.lock()) {
        std::cout << "[weak_ptr] 对象还活着，value = " << *locked << '\n';
    }
    value.reset();
    if (observer.expired()) {
        std::cout << "[weak_ptr] 对象已释放，weak_ptr 不延长其生命周期\n";
    }
}

}  // namespace

int main() {
    std::cout << "== 1. 手写 RAII 守卫 ==\n";
    {
        ResourceGuard guard("db-connection");
    }
    std::cout << "\n== 2. unique_ptr + 自定义 deleter ==\n";
    unique_ptr_demo();
    std::cout << "\n== 3. shared_ptr / weak_ptr ==\n";
    shared_ptr_demo();
    weak_ptr_demo();
    return 0;
}
