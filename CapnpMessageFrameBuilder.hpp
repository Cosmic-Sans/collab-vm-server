#pragma once
#include <vector>

template <typename Allocator = std::allocator<uint32_t>>
class CapnpMessageFrameBuilder final {
 public:
  CapnpMessageFrameBuilder(const Allocator& alloc = Allocator())
      : frame_(alloc) {}

  void Init(size_t segmentCount) {
    frame_.resize((segmentCount + 2) & ~size_t(1));
    frame_[0] = segmentCount - 1;
    segment_num_ = 1;
  }

  // TODO: Remove parameter?
  void Finalize(size_t segmentCount) {
    if (segmentCount % 2 == 0) {
      // Set padding byte
      frame_[segmentCount + 1] = 0;
    }
  }

  void AddSegment(size_t segmentSize) { frame_[segment_num_++] = segmentSize; }

  const std::vector<uint32_t, Allocator>& GetFrame() { return frame_; }

 private:
  std::vector<uint32_t, Allocator> frame_;
  size_t segment_num_;
};
