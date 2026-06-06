import { useState } from "react";
import { AnimatePresence, motion } from "motion/react";
import {
  ArrowRight,
  Database,
  FolderGit2,
  HardDrive,
  Plus,
  Trash2,
} from "lucide-react";

const SOURCE_URL = "https://github.com/randomfunction/LSMTree_engine";
const WRITE_DELAY_MS = 420;
const TRANSITION_DELAY_MS = 650;
const FLUSH_DELAY_MS = 950;
const COMPACTION_DELAY_MS = 1050;
const DELETE_SENTINEL = "TOMBSTONE";

let nextId = 0;

function makeId(prefix) {
  nextId += 1;
  return prefix + "-" + String(nextId);
}

function wait(delay) {
  return new Promise(function resolveAfterDelay(resolve) {
    window.setTimeout(resolve, delay);
  });
}

function sortEntriesByKey(left, right) {
  return left.key.localeCompare(right.key);
}

function upsertMemtable(entries, nextEntry) {
  const filteredEntries = entries.filter(function filterEntry(entry) {
    return entry.key !== nextEntry.key;
  });
  const updatedEntries = filteredEntries.concat(nextEntry);
  updatedEntries.sort(sortEntriesByKey);
  return updatedEntries;
}

function cloneItems(items, prefix) {
  return items.map(function cloneItem(item) {
    return {
      id: makeId(prefix),
      key: item.key,
      value: item.value,
      isDeleted: item.isDeleted,
    };
  });
}

function compactTables(tables) {
  const newestVisibleEntries = {};

  for (let tableIndex = 0; tableIndex < tables.length; tableIndex += 1) {
    const table = tables[tableIndex];

    for (let itemIndex = 0; itemIndex < table.items.length; itemIndex += 1) {
      const item = table.items[itemIndex];
      if (!newestVisibleEntries[item.key]) {
        newestVisibleEntries[item.key] = item;
      }
    }
  }

  const mergedKeys = Object.keys(newestVisibleEntries).sort();
  const compactedItems = [];

  for (let keyIndex = 0; keyIndex < mergedKeys.length; keyIndex += 1) {
    const key = mergedKeys[keyIndex];
    const item = newestVisibleEntries[key];

    if (item.isDeleted) {
      continue;
    }

    compactedItems.push({
      id: makeId("compacted-item"),
      key: item.key,
      value: item.value,
      isDeleted: false,
    });
  }

  return compactedItems;
}

function formatOperation(entry) {
  if (entry.type === "DELETE") {
    return "DELETE " + entry.key;
  }

  return "SET " + entry.key + "=" + entry.value;
}

function tableLabelFromNumber(number, compacted) {
  if (compacted) {
    return "SSTable " + String(number).padStart(2, "0") + " / compacted";
  }

  return "SSTable " + String(number).padStart(2, "0");
}

function StatusPill(props) {
  return (
    <motion.div
      layout
      className="inline-flex items-center gap-2 rounded-full border border-sky-300/50 bg-white/80 px-4 py-1.5 text-xs uppercase tracking-[0.24em] text-sky-800 shadow-[0_8px_24px_rgba(14,165,233,0.12)]"
    >
      <span className="h-2.5 w-2.5 rounded-full bg-sky-500 shadow-[0_0_14px_rgba(14,165,233,0.45)]" />
      {props.children}
    </motion.div>
  );
}

function SectionFrame(props) {
  return (
    <motion.section
      layout
      className="relative overflow-hidden rounded-[2rem] border border-slate-200/80 bg-white/80 p-6 shadow-[0_24px_80px_rgba(148,163,184,0.22)] backdrop-blur"
    >
      <div className="mb-4 flex items-start justify-between gap-3">
        <div>
          <div className="mb-2 flex items-center gap-2 text-slate-900">
            {props.icon}
            <h2 className="text-base uppercase tracking-[0.3em] text-slate-700">
              {props.title}
            </h2>
          </div>
          {props.subtitle ? (
            <p className="max-w-sm text-sm leading-7 text-slate-600">
              {props.subtitle}
            </p>
          ) : null}
        </div>
        {props.badge}
      </div>
      {props.children}
    </motion.section>
  );
}

function EntryCard(props) {
  const toneClassName = props.isDeleted
    ? "border-amber-300/70 bg-amber-50 text-amber-950"
    : "border-cyan-200/80 bg-cyan-50 text-cyan-950";

  return (
    <motion.div
      layout
      initial={{ opacity: 0, y: 18, scale: 0.96 }}
      animate={{ opacity: 1, y: 0, scale: 1 }}
      exit={{ opacity: 0, y: -18, scale: 0.94 }}
      transition={{ duration: 0.24 }}
      className={
        "rounded-2xl border px-4 py-4 text-sm shadow-[0_12px_30px_rgba(148,163,184,0.18)] " +
        toneClassName
      }
    >
      <div className="flex items-center justify-between gap-3">
        <span className="truncate font-semibold">{props.label}</span>
        <span className="text-[11px] uppercase tracking-[0.28em] opacity-70">
          {props.meta}
        </span>
      </div>
      <div className="mt-2 text-xs leading-6 text-slate-700">
        {props.value}
      </div>
    </motion.div>
  );
}

function SSTableBlock(props) {
  return (
    <motion.div
      layout
      initial={{ opacity: 0, x: 40, scale: 0.95 }}
      animate={{ opacity: 1, x: 0, scale: 1 }}
      exit={{ opacity: 0, x: -24, scale: 0.92 }}
      transition={{ type: "spring", stiffness: 240, damping: 24 }}
      className="relative rounded-3xl border border-blue-200/80 bg-gradient-to-br from-white via-sky-50 to-blue-100 p-5 shadow-[0_18px_50px_rgba(148,163,184,0.2)]"
    >
      <div className="mb-3 flex items-center justify-between gap-3">
        <div>
          <div className="text-sm uppercase tracking-[0.28em] text-blue-800">
            {props.label}
          </div>
          <div className="mt-1 text-xs text-slate-600">
            {props.items.length} sorted entries
          </div>
        </div>
        <span className="rounded-full border border-blue-200 bg-white/80 px-3 py-1 text-[11px] uppercase tracking-[0.28em] text-blue-700">
          immutable
        </span>
      </div>

      <div className="space-y-2">
        <AnimatePresence mode="popLayout">
          {props.items.map(function renderItem(item) {
            return (
              <EntryCard
                key={item.id}
                isDeleted={item.isDeleted}
                label={item.key}
                meta={item.isDeleted ? "delete" : "value"}
                value={item.isDeleted ? DELETE_SENTINEL : item.value}
              />
            );
          })}
        </AnimatePresence>
      </div>
    </motion.div>
  );
}

export default function App() {
  const [inputKey, setInputKey] = useState("");
  const [inputValue, setInputValue] = useState("");
  const [walEntries, setWalEntries] = useState([]);
  const [memtableEntries, setMemtableEntries] = useState([]);
  const [sstables, setSSTables] = useState([]);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("Idle");
  const [nextTableNumber, setNextTableNumber] = useState(1);
  const [flushingEntries, setFlushingEntries] = useState([]);
  const [compactingTables, setCompactingTables] = useState([]);

  async function runCompaction(tablesSnapshot, tableNumber) {
    const tablesToMerge = tablesSnapshot.slice(0, 3);
    const mergedItems = compactTables(tablesToMerge);
    const mergedTable = {
      id: makeId("sstable"),
      label: tableLabelFromNumber(tableNumber, true),
      items: mergedItems,
    };
    const remainingTables = tablesSnapshot.slice(3);
    const compactedTables = [mergedTable].concat(remainingTables);

    setStatus("Compaction: merging three SSTables into one");
    setCompactingTables(tablesToMerge);
    await wait(COMPACTION_DELAY_MS);
    setSSTables(compactedTables);
    setCompactingTables([]);
    setNextTableNumber(tableNumber + 1);
    await wait(240);
    setStatus("Idle");
    setBusy(false);
  }

  async function runFlush(memtableSnapshot) {
    const nextTable = {
      id: makeId("sstable"),
      label: tableLabelFromNumber(nextTableNumber, false),
      items: cloneItems(memtableSnapshot, "sstable-item"),
    };
    const nextTables = [nextTable].concat(sstables);

    setStatus("Flush: sorted MemTable is moving into a new SSTable");
    setFlushingEntries(cloneItems(memtableSnapshot, "flush-preview"));
    await wait(FLUSH_DELAY_MS);
    setMemtableEntries([]);
    setWalEntries([]);
    setSSTables(nextTables);
    setFlushingEntries([]);

    if (nextTables.length >= 3) {
      await wait(280);
      await runCompaction(nextTables, nextTableNumber + 1);
      return;
    }

    setNextTableNumber(nextTableNumber + 1);
    await wait(240);
    setStatus("Idle");
    setBusy(false);
  }

  async function handleWrite(type) {
    const trimmedKey = inputKey.trim();
    const trimmedValue = inputValue.trim();

    if (busy || trimmedKey.length === 0) {
      return;
    }

    if (type === "SET" && trimmedValue.length === 0) {
      return;
    }

    const walEntry = {
      id: makeId("wal"),
      type: type,
      key: trimmedKey,
      value: type === "SET" ? trimmedValue : "",
    };
    const memtableEntry = {
      id: makeId("memtable"),
      key: trimmedKey,
      value: type === "SET" ? trimmedValue : DELETE_SENTINEL,
      isDeleted: type === "DELETE",
    };
    const nextWalEntries = walEntries.concat(walEntry);
    const nextMemtableEntries = upsertMemtable(memtableEntries, memtableEntry);

    setBusy(true);
    setWalEntries(nextWalEntries);
    setStatus(type + ": appending to WAL");
    await wait(WRITE_DELAY_MS);
    setMemtableEntries(nextMemtableEntries);
    setStatus(type + ": applying into MemTable");

    setInputKey("");
    if (type === "SET") {
      setInputValue("");
    }

    await wait(TRANSITION_DELAY_MS);

    if (nextMemtableEntries.length >= 4) {
      await runFlush(nextMemtableEntries);
      return;
    }

    setStatus("Idle");
    setBusy(false);
  }

  function handleReset() {
    if (busy) {
      return;
    }

    setInputKey("");
    setInputValue("");
    setWalEntries([]);
    setMemtableEntries([]);
    setSSTables([]);
    setFlushingEntries([]);
    setCompactingTables([]);
    setNextTableNumber(1);
    setStatus("Idle");
  }

  const sourceLinkReady = SOURCE_URL !== "[INSERT_LINK]";

  return (
    <div className="min-h-screen w-full px-4 py-4 text-slate-900 sm:px-6 lg:px-8 xl:px-10">
      <div className="mx-auto w-full max-w-none">
        <motion.header
          initial={{ opacity: 0, y: -18 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.45 }}
          className="mb-6 rounded-[2.2rem] border border-slate-200/80 bg-white/86 p-7 shadow-[0_30px_80px_rgba(148,163,184,0.2)] backdrop-blur"
        >
          <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
            <div>
              <h1 className="text-4xl font-semibold tracking-tight text-slate-950 sm:text-5xl">
                LSM-Tree Engine Flow
              </h1>
              <p className="mt-3 max-w-4xl text-base leading-8 text-slate-600">
                Watch writes land in the WAL, settle into a sorted MemTable,
                flush to immutable SSTables, and compact back into one clean disk view.
              </p>
              <p className="mt-3 max-w-4xl text-base leading-8 text-slate-600">
                <b>This only a simulation, the code is in C++, Click View Source Button</b>
              </p>
            </div>

            <div className="flex flex-wrap items-center gap-3">
              <StatusPill>{status}</StatusPill>
              <motion.a
                whileHover={sourceLinkReady ? { y: -2 } : undefined}
                whileTap={sourceLinkReady ? { scale: 0.98 } : undefined}
                href={sourceLinkReady ? SOURCE_URL : undefined}
                target="_blank"
                rel="noreferrer"
                className={
                  "inline-flex items-center gap-2 rounded-full border px-5 py-3 text-base transition " +
                  (sourceLinkReady
                    ? "border-slate-200 bg-white text-slate-800 hover:border-sky-300/70 hover:bg-sky-50"
                    : "cursor-not-allowed border-slate-200 bg-slate-100 text-slate-400")
                }
                title={
                  sourceLinkReady
                    ? "Open repository"
                    : "Replace [INSERT_LINK] in App.jsx to enable this button"
                }
              >
                <FolderGit2 className="h-4 w-4" />
                View Source
              </motion.a>
            </div>
          </div>
        </motion.header>

        <div className="grid gap-6 xl:grid-cols-[340px_minmax(0,1.08fr)_minmax(0,1.12fr)]">
          <SectionFrame
            icon={<ArrowRight className="h-5 w-5 text-sky-600" />}
            title="Controls"
            subtitle="Create point writes and delete tombstones. The visualizer runs the write path step by step so you can watch the engine state change."
            badge={<StatusPill>{busy ? "Running" : "Ready"}</StatusPill>}
          >
            <div className="space-y-4">
              <label className="block">
                <span className="mb-2 block text-xs uppercase tracking-[0.28em] text-slate-600">
                  Key
                </span>
                <input
                  value={inputKey}
                  onChange={function updateKey(event) {
                    setInputKey(event.target.value);
                  }}
                  placeholder="user:42"
                  className="w-full rounded-2xl border border-slate-200 bg-white px-4 py-3.5 text-base text-slate-900 outline-none transition focus:border-sky-400 focus:bg-white"
                />
              </label>

              <label className="block">
                <span className="mb-2 block text-xs uppercase tracking-[0.28em] text-slate-600">
                  Value
                </span>
                <input
                  value={inputValue}
                  onChange={function updateValue(event) {
                    setInputValue(event.target.value);
                  }}
                  placeholder="alice"
                  className="w-full rounded-2xl border border-slate-200 bg-white px-4 py-3.5 text-base text-slate-900 outline-none transition focus:border-sky-400 focus:bg-white"
                />
              </label>

              <div className="grid grid-cols-2 gap-3">
                <motion.button
                  whileHover={!busy ? { y: -2 } : undefined}
                  whileTap={!busy ? { scale: 0.98 } : undefined}
                  onClick={function onSetClick() {
                    handleWrite("SET");
                  }}
                  disabled={busy || inputKey.trim().length === 0 || inputValue.trim().length === 0}
                  className="inline-flex items-center justify-center gap-2 rounded-2xl border border-cyan-300 bg-cyan-50 px-4 py-3.5 text-base text-cyan-900 transition disabled:cursor-not-allowed disabled:border-slate-200 disabled:bg-slate-100 disabled:text-slate-400"
                >
                  <Plus className="h-4 w-4" />
                  SET
                </motion.button>

                <motion.button
                  whileHover={!busy ? { y: -2 } : undefined}
                  whileTap={!busy ? { scale: 0.98 } : undefined}
                  onClick={function onDeleteClick() {
                    handleWrite("DELETE");
                  }}
                  disabled={busy || inputKey.trim().length === 0}
                  className="inline-flex items-center justify-center gap-2 rounded-2xl border border-amber-300 bg-amber-50 px-4 py-3.5 text-base text-amber-900 transition disabled:cursor-not-allowed disabled:border-slate-200 disabled:bg-slate-100 disabled:text-slate-400"
                >
                  <Trash2 className="h-4 w-4" />
                  DELETE
                </motion.button>
              </div>

              <motion.button
                whileHover={!busy ? { y: -2 } : undefined}
                whileTap={!busy ? { scale: 0.98 } : undefined}
                onClick={handleReset}
                disabled={busy}
                className="w-full rounded-2xl border border-slate-200 bg-slate-50 px-4 py-3.5 text-base text-slate-700 transition hover:border-sky-200 hover:bg-sky-50 disabled:cursor-not-allowed disabled:text-slate-400"
              >
                Reset visualizer
              </motion.button>

              <div className="rounded-2xl border border-slate-200 bg-slate-50/90 p-5 text-sm leading-7 text-slate-600">
                <div className="mb-2 uppercase tracking-[0.28em] text-slate-500">
                  Rules
                </div>
                <div>1. Every write appends to the WAL first.</div>
                <div>2. MemTable stays alphabetically sorted by key.</div>
                <div>3. Flush starts when the MemTable reaches 4 items.</div>
                <div>4. Compaction starts when disk reaches 3 SSTables.</div>
              </div>
            </div>
          </SectionFrame>

          <div className="space-y-5">
            <SectionFrame
              icon={<Database className="h-5 w-5 text-cyan-600" />}
              title="RAM"
              subtitle="The in-memory path is split into an append-only WAL for durability and a sorted MemTable for fast reads and flush preparation."
              badge={<StatusPill>{String(walEntries.length + memtableEntries.length)} live entries</StatusPill>}
            >
              <div className="grid gap-4 xl:grid-cols-2">
                <div className="rounded-[1.9rem] border border-slate-200 bg-slate-50/80 p-5">
                  <div className="mb-3 flex items-center justify-between">
                    <div>
                      <div className="text-sm uppercase tracking-[0.32em] text-slate-700">
                        WAL
                      </div>
                      <div className="mt-1 text-xs text-slate-500">
                        append-only log
                      </div>
                    </div>
                    <span className="rounded-full border border-slate-200 bg-white px-3 py-1 text-[11px] uppercase tracking-[0.28em] text-slate-600">
                      {walEntries.length} ops
                    </span>
                  </div>

                  <div className="space-y-2">
                    <AnimatePresence mode="popLayout">
                      {walEntries.map(function renderWalEntry(entry) {
                        return (
                          <EntryCard
                            key={entry.id}
                            isDeleted={entry.type === "DELETE"}
                            label={entry.key}
                            meta={entry.type}
                            value={formatOperation(entry)}
                          />
                        );
                      })}
                    </AnimatePresence>

                    {walEntries.length === 0 ? (
                      <div className="rounded-2xl border border-dashed border-slate-300 px-4 py-5 text-sm text-slate-500">
                        The WAL is empty until a write arrives.
                      </div>
                    ) : null}
                  </div>
                </div>

                <div className="relative rounded-[1.9rem] border border-slate-200 bg-slate-50/80 p-5">
                  <div className="mb-3 flex items-center justify-between">
                    <div>
                      <div className="text-sm uppercase tracking-[0.32em] text-slate-700">
                        MemTable
                      </div>
                      <div className="mt-1 text-xs text-slate-500">
                        sorted in memory
                      </div>
                    </div>
                    <span className="rounded-full border border-slate-200 bg-white px-3 py-1 text-[11px] uppercase tracking-[0.28em] text-slate-600">
                      {memtableEntries.length}/4
                    </span>
                  </div>

                  <div className="space-y-2">
                    <AnimatePresence mode="popLayout">
                      {memtableEntries.map(function renderMemtableEntry(entry) {
                        return (
                          <EntryCard
                            key={entry.id}
                            isDeleted={entry.isDeleted}
                            label={entry.key}
                            meta="sorted"
                            value={entry.isDeleted ? DELETE_SENTINEL : entry.value}
                          />
                        );
                      })}
                    </AnimatePresence>

                    {memtableEntries.length === 0 ? (
                      <div className="rounded-2xl border border-dashed border-slate-300 px-4 py-5 text-sm text-slate-500">
                        New writes settle here after the WAL append animation.
                      </div>
                    ) : null}
                  </div>
                </div>
              </div>
            </SectionFrame>
          </div>

          <SectionFrame
            icon={<HardDrive className="h-5 w-5 text-blue-600" />}
            title="Disk"
            subtitle="Flushed MemTable contents become immutable SSTables. When three blocks accumulate, compaction merges them into one newest-view snapshot."
            badge={<StatusPill>{String(sstables.length)} SSTables</StatusPill>}
          >
            <div className="relative min-h-[38rem] rounded-[1.9rem] border border-slate-200 bg-slate-50/80 p-5">
              <AnimatePresence>
                {flushingEntries.length > 0 ? (
                  <motion.div
                    key="flush-overlay"
                    initial={{ opacity: 0, x: -180, y: 30, scale: 0.92 }}
                    animate={{ opacity: 1, x: 0, y: 0, scale: 1 }}
                    exit={{ opacity: 0, x: 70, y: -24, scale: 0.95 }}
                    transition={{ duration: 0.7, ease: [0.2, 0.9, 0.2, 1] }}
                    className="pointer-events-none absolute inset-x-4 top-4 z-20 rounded-3xl border border-cyan-300/50 bg-cyan-50/95 p-5 shadow-[0_24px_60px_rgba(8,145,178,0.14)]"
                  >
                    <div className="mb-3 flex items-center justify-between text-sm uppercase tracking-[0.28em] text-cyan-800">
                      <span>Flush in progress</span>
                      <ArrowRight className="h-4 w-4" />
                    </div>
                    <div className="space-y-2">
                      {flushingEntries.map(function renderFlushingEntry(entry) {
                        return (
                          <EntryCard
                            key={entry.id}
                            isDeleted={entry.isDeleted}
                            label={entry.key}
                            meta="moving"
                            value={entry.isDeleted ? DELETE_SENTINEL : entry.value}
                          />
                        );
                      })}
                    </div>
                  </motion.div>
                ) : null}
              </AnimatePresence>

              <AnimatePresence>
                {compactingTables.length > 0 ? (
                  <motion.div
                    key="compaction-overlay"
                    initial={{ opacity: 0, scale: 0.94 }}
                    animate={{ opacity: 1, scale: 1 }}
                    exit={{ opacity: 0, scale: 1.04 }}
                    transition={{ duration: 0.55 }}
                    className="pointer-events-none absolute inset-4 z-20 rounded-[1.8rem] border border-blue-300/40 bg-blue-50/95 p-5 shadow-[0_26px_80px_rgba(37,99,235,0.14)]"
                  >
                    <div className="mb-4 flex items-center justify-between text-sm uppercase tracking-[0.3em] text-blue-800">
                      <span>Compaction in progress</span>
                      <ArrowRight className="h-4 w-4" />
                    </div>
                    <div className="grid gap-3">
                      {compactingTables.map(function renderCompactingTable(table, index) {
                        return (
                          <motion.div
                            key={table.id}
                            initial={{ x: index * 16, opacity: 0.75 }}
                            animate={{ x: index === 0 ? 0 : 8 - index * 6, opacity: 1 }}
                            className="rounded-3xl border border-blue-200 bg-white/90 p-4"
                          >
                            <div className="mb-2 text-[11px] uppercase tracking-[0.28em] text-blue-700">
                              {table.label}
                            </div>
                            <div className="space-y-2">
                              {table.items.slice(0, 3).map(function renderCompactItem(item) {
                                return (
                                  <EntryCard
                                    key={item.id}
                                    isDeleted={item.isDeleted}
                                    label={item.key}
                                    meta="merge"
                                    value={item.isDeleted ? DELETE_SENTINEL : item.value}
                                  />
                                );
                              })}
                            </div>
                          </motion.div>
                        );
                      })}
                    </div>
                  </motion.div>
                ) : null}
              </AnimatePresence>

              <div className="space-y-4">
                <AnimatePresence mode="popLayout">
                  {sstables.map(function renderSSTable(table) {
                    return <SSTableBlock key={table.id} label={table.label} items={table.items} />;
                  })}
                </AnimatePresence>

                {sstables.length === 0 ? (
                  <motion.div
                    initial={{ opacity: 0 }}
                    animate={{ opacity: 1 }}
                    className="rounded-[1.6rem] border border-dashed border-slate-300 bg-white/60 px-5 py-12 text-base leading-8 text-slate-500"
                  >
                    Disk is empty. Reach four MemTable items to trigger the first flush into an immutable SSTable block.
                  </motion.div>
                ) : null}
              </div>
            </div>
          </SectionFrame>
        </div>
      </div>
    </div>
  );
}
