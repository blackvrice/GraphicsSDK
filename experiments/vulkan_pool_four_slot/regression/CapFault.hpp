// Included by the isolated churn consumer after its Context and data helpers.
void capped(Context& context) {
    constexpr unsigned size = 4096;
    for (unsigned round = 0; round < 2; ++round) {
        const int oom = round == 0 ? -1 : -2;
        std::vector<Item> guards;
        context.fault.set(1, round);
        for (unsigned i = 0; i < 7; ++i) {
            guards.push_back(context.make(size, 100 * round + i + 1));
        }
        verifyAll(context, guards, true);
        context.capture("cap-full", round);
        const auto full = context.snapshot();
        check(full->buffers == 7 && full->memories == 3 && full->bytes == 7 * size,
              "Fill the 1, 2 and 4 slot blocks before capped growth");

        context.fault.set(2, round);
        check(context.fault.arm(oom, 0, 0, 1) != 0, "Arm capped growth OOM");
        auto fallback = take(context.d.createBuffer({size}));
        check(!context.fault.status(), "Capped growth OOM consumed");
        const auto after = context.snapshot();
        check(after->buffers == 8 && after->memories == 4 && after->bytes == full->bytes + size,
              "Fallback reserves only the exact requirement");
        for (unsigned i = 0; i < after->count; ++i) {
            const auto& entry = after->entries[i];
            const auto known = context.model.live.find(entry.buffer);
            if (known != context.model.live.end()) {
                check(known->second.entry == entry, "Fallback preserves existing bindings");
            } else {
                check(entry.offset == 0 && entry.allocation == size && entry.required == size,
                      "Fallback is an independent exact-size allocation");
            }
        }
        verifyAll(context, {{fallback, size, 100 * round + 30}}, true);
        fallback.reset();
        context.capture("fallback-released", round);

        context.fault.set(2, round);
        check(context.fault.arm(oom, -2, 0, 1) != 0, "Arm growth and exact fallback OOM");
        const auto failed = context.d.createBuffer({size});
        check(!failed && failed.error().code == gfx::ErrorCode::OutOfMemory && failed.error().nativeCode == -2,
              "Report the exact fallback failure");
        check(!context.fault.status(), "Both allocation faults consumed without unsafe cleanup");
        context.capture("rollback", round);

        context.fault.set(3, round);
        guards.push_back(context.make(size, 100 * round + 40));
        verifyAll(context, {guards.back()}, true);
        verifyAll(context, guards, false);
        context.capture("retry", round);
        const auto retry = context.snapshot();
        check(retry->buffers == 8 && retry->memories == 4 && retry->bytes == full->bytes + 4 * size,
              "Retry grows by four slots again");
        context.fault.set(4, round);
        clear(context, guards);
        context.capture("empty", round);
        context.fault.set(0, round);
    }
}
