// Copyright (c) 2023-2026 Manuel Schneider

#include "generatorqueryhandler.h"
#include <QCoroAsyncGenerator>
#include <QCoroFuture>
#include <QCoroGenerator>
#include <QtConcurrentRun>
using namespace Qt::StringLiterals;
using namespace albert;
using namespace std;

GeneratorQueryHandler::~GeneratorQueryHandler() {}

AsyncItemGenerator GeneratorQueryHandler::asyncItemGenerator(QueryContext &context)
{
    // Block coro stack frame undwinding for the thread lifetime.
    struct ThreadJoiner {
        QFuture<void> future;
        ~ThreadJoiner()
        {
            if (!future.isFinished())
                future.waitForFinished();
        }
    } thread_joiner;

    ItemGenerator sync_item_generator;
    ItemGenerator::iterator it = sync_item_generator.end();

    thread_joiner.future = QtConcurrent::run([&] {
        // `items()` could also be a regular function that returns a generator.
        // This function should as well run in the thread.
        sync_item_generator = items(context);
        it = sync_item_generator.begin();
    });

    co_await thread_joiner.future;

    while (it != sync_item_generator.end())
    {
        co_yield ::move(*it);
        thread_joiner.future = QtConcurrent::run([&] { ++it; });
        co_await thread_joiner.future;
    }
}


// -------------------------------------------------------------------------------------------------
// Old implementation
// -------------------------------------------------------------------------------------------------

// class GeneratorQueryHandlerExecution final : public QueryExecution
// {
//     QFutureWatcher<vector<shared_ptr<Item>>> watcher;  // implicit active flag
//     GeneratorQueryHandler &handler;
//     ItemGenerator generator;  // mutexed
//     optional<ItemGenerator::iterator> iterator;  // mutexed
//     bool active;
//     // items(), begin and operator++ are potentially long blocking operations.
//     // it had to be mutexed because canFetchMore may check the iterator in the main thread.
//     // awaiting the lock however blocks the main thread potentially long.
//     // store a simple atomic at_end flag to avoid this.
//     // now theres only generator and iterator left that are touched in the thread
//     // due to the active flag they will never run concurrently
//     // so we dont actually need to mutex them at all
//     atomic_bool at_end;

// public:

//     GeneratorQueryHandlerExecution(QueryContext &ctx, GeneratorQueryHandler &h)
//         : QueryExecution(ctx)
//         , handler(h)
//         , iterator(nullopt)
//         , active(true)
//         , at_end(false)
//     {
//         connect(&watcher, &QFutureWatcher<void>::finished,
//                 this, &GeneratorQueryHandlerExecution::onFetchFinished);

//         watcher.setFuture(QtConcurrent::run([this] -> vector<shared_ptr<Item>>
//         {
//             // `items()` could also be a regular function that returns a generator.
//             // This function should as well run in the thread.
//             generator = handler.items(context);
//             iterator = generator.begin();
//             if (iterator != generator.end())
//                 return ::move(*iterator.value());
//             return {};
//         }));
//     }

//     ~GeneratorQueryHandlerExecution()
//     {
//         cancel();

//         // Qt 6.4 QFutureWatcher is broken.
//         // isFinished returns wrong values and waitForFinished blocks forever on finished futures.
//         // TODO(26.04): Remove workaround when dropping Qt < 6.5 support.
// #if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
//         if (!watcher.isFinished())
// #else
//         if (watcher.isRunning())
// #endif
//         {
//             DEBG << QString("Busy wait on query: #%1").arg(id);
//             watcher.waitForFinished();
//         }
//     }

//     void cancel() override { }

//     bool isActive() const override { return active; }

//     bool canFetchMore() const override { return context.isValid() && !at_end; }

//     void fetchMore() override
//     {
//         if (!isActive() && canFetchMore())
//         {
//             emit activeChanged(active = true);
//             watcher.setFuture(QtConcurrent::run([this] -> vector<shared_ptr<Item>>
//             {
//                 ++*iterator;
//                 if (iterator != generator.end())
//                     return ::move(*iterator.value());
//                 return {};
//             }));
//         }
//     }

//     void onFetchFinished()
//     {
//         if (context.isValid())
//             try {
//                 try {
//                     auto items = watcher.future().takeResult();
//                     if (items.empty())
//                         at_end = true;
//                     else
//                         results.add(::move(items));
//                 } catch (const QUnhandledException &que) {
//                     if (que.exception())
//                         rethrow_exception(que.exception());
//                     else
//                         throw runtime_error("QUnhandledException::exception() returned nullptr.");
//                 }
//             } catch (const exception &e) {
//                 WARN << u"GeneratorQueryHandler threw exception:\n"_s << e.what();
//             } catch (...) {
//                 WARN << u"GeneratorQueryHandler threw unknown exception."_s;
//             }

//         emit activeChanged(active = false);
//     }
// };

// unique_ptr<QueryExecution> GeneratorQueryHandler::execution(QueryContext &ctx)
// { return make_unique<GeneratorQueryHandlerExecution>(ctx, *this); }

