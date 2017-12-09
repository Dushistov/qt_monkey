#pragma once

#include <mutex>

namespace qt_monkey_common
{
template <typename DataType> class SharedResource final
{
public:
    struct DataPtrWrapper final {
        DataPtrWrapper() = delete;
        DataPtrWrapper(const DataPtrWrapper &) = delete;
        DataPtrWrapper(DataPtrWrapper &&o): resource_(o.resource_), moved_(o.moved_) {
            o.moved_ = true;
        }
        DataPtrWrapper &operator=(const DataPtrWrapper &) = delete;
        ~DataPtrWrapper() {
            if (!moved_) {
                resource_.dataLock_.unlock();
            }
        }
        DataType *operator->() { return &resource_.data_; }
        DataType &operator*() { return resource_.data_; }
    private:
        SharedResource &resource_;
        bool moved_;
        friend class SharedResource;
        DataPtrWrapper(SharedResource &resource) : resource_(resource), moved_(false)
        {
            resource_.dataLock_.lock();
        }
    };
    SharedResource() = default;
    explicit SharedResource(const DataType &d) : data_(d) {}
    explicit SharedResource(DataType &&d) : data_(std::move(d)) {}
    SharedResource &operator=(const SharedResource &) = delete;
    SharedResource(const SharedResource &) = delete;
    DataPtrWrapper get() { return DataPtrWrapper(*this); }
private:
    DataType data_;
    std::mutex dataLock_;
};
}
