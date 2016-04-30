#pragma once

#include <mutex>

namespace qt_monkey_agent
{
template <typename DataType> class SharedResource final
{
public:
    struct DataPtrWrapper final {
        ~DataPtrWrapper() { resource_.dataLock_.unlock(); }
        DataType *operator->() { return &resource_.data_; }
        DataType &operator*() { return resource_.data_; }
    private:
        SharedResource &resource_;
        friend class SharedResource;
        DataPtrWrapper(SharedResource &resource) : resource_(resource)
        {
            resource_.dataLock_.lock();
        }
    };
    SharedResource() = default;
    explicit SharedResource(const DataType &d) : data_(d) {}
    explicit SharedResource(DataType &&d) : data_(std::move(d)) {}
    DataPtrWrapper get() { return DataPtrWrapper(*this); }
private:
    DataType data_;
    std::mutex dataLock_;
};
}
