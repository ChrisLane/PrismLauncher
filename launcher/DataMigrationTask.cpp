#include "DataMigrationTask.h"

#include "FileSystem.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QMap>

#include <QtConcurrent>

DataMigrationTask::DataMigrationTask(QObject* parent,
                                     const QString& sourcePath,
                                     const QString& targetPath,
                                     const IPathMatcher::Ptr pathMatcher)
    : Task(parent), m_sourcePath(sourcePath), m_targetPath(targetPath), m_pathMatcher(pathMatcher), m_copy(sourcePath, targetPath)
{
    m_copy.matcher(m_pathMatcher.get()).whitelist(true);
}

void DataMigrationTask::executeTask()
{
    setStatus(tr("Scanning files..."));

    // 1. Scan
    // Check how many files we gotta copy
    m_copyFuture = QtConcurrent::run(QThreadPool::globalInstance(), [&] {
        return m_copy(true);  // dry run to collect amount of files
    });
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::dryRunFinished);
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::dryRunAborted);
    m_copyFutureWatcher.setFuture(m_copyFuture);
}

void DataMigrationTask::dryRunFinished()
{
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::dryRunFinished);
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::dryRunAborted);

    if (!m_copyFuture.result()) {
        emitFailed("Some error");  // FIXME
        return;
    }

    setStatus(tr("Migrating..."));

    // 2. Copy
    // Actually copy all files now.
    m_toCopy = m_copy.totalCopied();
    connect(&m_copy, &FS::copy::fileCopied, [&, this] { setProgress(m_copy.totalCopied(), m_toCopy); });
    m_copyFuture = QtConcurrent::run(QThreadPool::globalInstance(), [&] {
        return m_copy(false);  // actually copy now
    });
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::copyFinished);
    connect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::copyAborted);
    m_copyFutureWatcher.setFuture(m_copyFuture);
}

void DataMigrationTask::dryRunAborted()
{
    emitFailed(tr("Aborted"));
}

void DataMigrationTask::copyFinished()
{
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::finished, this, &DataMigrationTask::copyFinished);
    disconnect(&m_copyFutureWatcher, &QFutureWatcher<bool>::canceled, this, &DataMigrationTask::copyAborted);

    if (!m_copyFuture.result()) {
        emitFailed("Some paths could not be copied!");
        return;
    }

    emitSucceeded();
}

void DataMigrationTask::copyAborted()
{
    emitFailed(tr("Aborted"));
}
