// REQUIRES: level_zero, gpu
// RUN: %{build} -o %t.out
// RUN: %{run} %t.out
//
// UNSUPPORTED: gpu-intel-pvc
// The test checks that invalid exception is thrown
// when trying to use sycl_ext_oneapi_device_global
// along with Graph.

#include "graph_common.hpp"

using TestProperties = decltype(sycl::ext::oneapi::experimental::properties{});

sycl::ext::oneapi::experimental::device_global<int, TestProperties>
    MemcpyDeviceGlobal;
sycl::ext::oneapi::experimental::device_global<int, TestProperties>
    CopyDeviceGlobal;

enum OperationPath { Explicit, RecordReplay, Shortcut };

template <OperationPath PathKind> void test() {
  queue Q;
  int MemcpyWrite = 42, CopyWrite = 24, MemcpyRead = 1, CopyRead = 2;

  exp_ext::command_graph Graph{Q.get_context(), Q.get_device()};

  if constexpr (PathKind != OperationPath::Explicit) {
    Graph.begin_recording(Q);
  }

  // Copy from device globals before having written anything.
  std::error_code ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
    }
    if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit([&](handler &CGH) {
        return CGH.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
      });
    }
    if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add([&](handler &CGH) {
        return CGH.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
      });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.copy(CopyDeviceGlobal, &CopyRead);
    }
    if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit(
          [&](handler &CGH) { return CGH.copy(CopyDeviceGlobal, &CopyRead); });
    }
    if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add(
          [&](handler &CGH) { return CGH.copy(CopyDeviceGlobal, &CopyRead); });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  // Write to device globals and then read their values.
  ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.memcpy(MemcpyDeviceGlobal, &MemcpyWrite);
    }
    if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit([&](handler &CGH) {
        return CGH.memcpy(MemcpyDeviceGlobal, &MemcpyWrite);
      });
    }
    if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add([&](handler &CGH) {
        return CGH.memcpy(MemcpyDeviceGlobal, &MemcpyWrite);
      });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.copy(&CopyWrite, CopyDeviceGlobal);
    } else if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit(
          [&](handler &CGH) { return CGH.copy(&CopyWrite, CopyDeviceGlobal); });
    } else if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add(
          [&](handler &CGH) { return CGH.copy(&CopyWrite, CopyDeviceGlobal); });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
    } else if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit([&](handler &CGH) {
        return CGH.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
      });
    } else if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add([&](handler &CGH) {
        return CGH.memcpy(&MemcpyRead, MemcpyDeviceGlobal);
      });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  ExceptionCode = make_error_code(sycl::errc::success);
  try {
    if constexpr (PathKind == OperationPath::Shortcut) {
      Q.copy(CopyDeviceGlobal, &CopyRead);
    } else if constexpr (PathKind == OperationPath::RecordReplay) {
      Q.submit(
          [&](handler &CGH) { return CGH.copy(CopyDeviceGlobal, &CopyRead); });
    } else if constexpr (PathKind == OperationPath::Explicit) {
      Graph.add(
          [&](handler &CGH) { return CGH.copy(CopyDeviceGlobal, &CopyRead); });
    }
  } catch (exception &Exception) {
    ExceptionCode = Exception.code();
  }
  assert(ExceptionCode == sycl::errc::invalid);

  if constexpr (PathKind != OperationPath::Explicit) {
    Graph.end_recording();
  }
}

int main() {
  test<OperationPath::Explicit>();
  test<OperationPath::RecordReplay>();
  test<OperationPath::Shortcut>();
  return 0;
}
