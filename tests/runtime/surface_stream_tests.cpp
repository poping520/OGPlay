#include <doctest/doctest.h>
#include "ogplay/runtime/integration/surface_stream.h"
#include <ostream>
#include <thread>

using namespace ogplay::runtime;
namespace {
SurfaceStreamFrame Image(std::int64_t pts, std::uint8_t value = 32) {
    SurfaceStreamFrame image; image.width = 2; image.height = 2;
    image.timestamp_ns = pts; image.rgba.assign(16, value); return image;
}
}
TEST_CASE("Surface stream queue enforces producer identity latest image and budget leases") {
    auto budget = std::make_shared<SurfaceStreamBudget>(48);
    SurfaceStream a(false, budget), b(false, budget);
    const auto producer = a.Connect(), other = b.Connect();
    CHECK_THROWS_AS(a.Connect(), std::logic_error);
    CHECK_THROWS_AS(b.Submit(producer, Image(1)), std::invalid_argument);
    CHECK(a.Submit(producer, Image(10)) == SurfaceStream::SubmitResult::queued);
    CHECK(a.Submit(producer, Image(20)) == SurfaceStream::SubmitResult::replaced);
    CHECK(a.TakeNotification()); CHECK_FALSE(a.TakeNotification());
    auto acquired = a.Acquire();
    REQUIRE(acquired.frame); CHECK(acquired.frame->timestamp_ns == 20);
    CHECK_THROWS_AS(b.Commit(acquired), std::invalid_argument);
    a.Commit(acquired); CHECK_FALSE(a.Pending().frame);
    CHECK(a.Submit(producer, Image(5)) == SurfaceStream::SubmitResult::queued);
    CHECK(a.TakeNotification()); // source PTS may rewind on seek
    auto pending = a.Pending();
    a.Disconnect(producer);
    CHECK_FALSE(a.Pending().frame); CHECK(a.Current().frame == acquired.frame);
    const auto fresh = a.Connect(); CHECK(fresh != producer);
    CHECK_THROWS_AS(a.Submit(producer, Image(30)), std::invalid_argument);
    CHECK(a.Submit(fresh, Image(30)) == SurfaceStream::SubmitResult::queued);
    CHECK_THROWS_AS(b.Submit(other, Image(40)), std::length_error);
    a.Abandon(); CHECK_FALSE(a.Live()); CHECK_FALSE(a.TakeNotification());
    CHECK_THROWS_AS(a.Submit(fresh, Image(50)), std::invalid_argument);
    CHECK(budget->Used() == 32); // snapshots still own their two distinct frames
    acquired = {}; pending = {}; CHECK(budget->Used() == 0);
    auto bad = Image(10); bad.rgba.pop_back();
    CHECK_THROWS_AS(b.Submit(other, std::move(bad)), std::invalid_argument);
}
TEST_CASE("Surface stream single buffer transfer is nonblocking and worker submission has no callbacks") {
    auto budget = std::make_shared<SurfaceStreamBudget>();
    SurfaceStream stream(true, budget); const auto producer = stream.Connect();
    CHECK(stream.DefaultSize() == std::array<std::uint32_t, 2>{1, 1});
    stream.SetDefaultSize(320, 240); stream.SetDefaultSize(0, 0);
    CHECK(stream.DefaultSize() == std::array<std::uint32_t, 2>{320, 240});
    std::thread worker([&] { CHECK(stream.Submit(producer, Image(1)) == SurfaceStream::SubmitResult::queued); });
    worker.join(); CHECK(stream.TakeNotification());
    CHECK(stream.Submit(producer, Image(2)) == SurfaceStream::SubmitResult::busy);
    stream.Commit(stream.Acquire());
    CHECK(stream.Submit(producer, Image(3)) == SurfaceStream::SubmitResult::busy);
    stream.ReleaseImage();
    CHECK(stream.Submit(producer, Image(4)) == SurfaceStream::SubmitResult::queued);
    stream.Abandon(); CHECK(budget->Used() == 0);
}

TEST_CASE("Surface stream acquisition preserves notifications across concurrent producer enqueue and rollback") {
    auto budget = std::make_shared<SurfaceStreamBudget>();
    SurfaceStream stream(false, budget); const auto producer = stream.Connect();
    CHECK(stream.Submit(producer, Image(1)) == SurfaceStream::SubmitResult::queued);
    CHECK(stream.TakeNotification());
    auto first = stream.Acquire();
    CHECK_FALSE(stream.Pending().frame);
    // Models a worker enqueue while GL uploads the acquired frame. It is a
    // new queue entry, not replacement of the frame already owned by GL.
    std::thread worker([&] { CHECK(stream.Submit(producer, Image(2)) == SurfaceStream::SubmitResult::queued); });
    worker.join(); stream.Commit(first);
    CHECK(stream.TakeNotification());
    CHECK(stream.Current().frame->timestamp_ns == 1);
    CHECK(stream.Pending().frame->timestamp_ns == 2);
    auto second = stream.Acquire();
    stream.Abort(second);
    CHECK(stream.Pending().frame == second.frame);
    CHECK(stream.Current().frame == first.frame);
    second = stream.Acquire();
    CHECK(stream.Submit(producer, Image(3)) == SurfaceStream::SubmitResult::queued);
    stream.Abort(second); // a newer pending image wins on failed upload
    CHECK(stream.Pending().frame->timestamp_ns == 3);
    auto latest = stream.Acquire(); stream.Commit(latest);
    CHECK(stream.Current().frame->timestamp_ns == 3);
    stream.Abandon(); first = {}; second = {}; latest = {};
    CHECK(budget->Used() == 0);
}
