#include "core/memory_tracker.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/spin_mutex.h"

#include <new>
#include <stdio.h>
#include <vadefs.h>
#include <Windows.h>

#ifdef MEM_TRACK

#undef min

namespace Lux
{
	void memTrackerLog(const char* system, const char* message, ...)
	{
		char tmp[1024];
		va_list args;
		va_start(args, message);
		vsnprintf(tmp, 1021, message, args);
		va_end(args);

		strcat(tmp, "\n");
		OutputDebugString(tmp);
	}

	// struct FILE_LINE_REPORT
	struct FileLineReport
	{
		const char *file;
		int32_t line;

		LUX_FORCE_INLINE bool operator == (const FileLineReport &other) const { return file == other.file && line == other.line; }
		LUX_FORCE_INLINE bool operator != (const FileLineReport &other) const { return !(*this == other); }

		LUX_FORCE_INLINE bool operator < (const FileLineReport &other) const
		{
			if(file == NULL)
				return other.file != NULL;
			if(other.file == NULL)
				return false;
			int cmp = strcmp(file, other.file);
			if(cmp != 0)
				return cmp < 0;
			return line < other.line;
		}

		LUX_FORCE_INLINE bool operator > (const FileLineReport &other) const
		{
			if(file == NULL)
				return other.file != NULL;
			if(other.file == NULL)
				return false;
			int cmp = strcmp(file, other.file);
			if(cmp != 0)
				return cmp > 0;
			return line > other.line;
		}
	};

	typedef map<uint32_t, MemoryTracker::Entry*, MemTrackAllocator> map_alloc_order;
	typedef map<FileLineReport, intptr_t, MemTrackAllocator> file_line_map;
	typedef map<const char *, intptr_t, MemTrackAllocator> file_map;
	typedef map<FileLineReport, uint32_t, MemTrackAllocator> alloc_count_map;

	MemoryTracker* MemoryTracker::s_instance = NULL;
	uint32_t MemoryTracker::s_alloc_counter = 0;

	MemoryTracker& MemoryTracker::getInstance()
	{
		_CrtSetDbgFlag(_CRTDBG_CHECK_DEFAULT_DF); // TODO: pc only
		s_instance = NULL != s_instance ? s_instance : new(malloc(sizeof(MemoryTracker))) MemoryTracker();
		return *s_instance;
	}

	void MemoryTracker::destruct()
	{
		s_instance->~MemoryTracker();
		free(s_instance);
		s_instance = NULL;
	}

	void MemoryTracker::add(void* p, const intptr_t size, const char* file, const int line)
	{
		if(!p) return;

		MT::SpinLock lock(m_spin_mutex);

		m_map.insert(p, Entry(file, line, size));
		m_allocated_memory += size;
	}

	void MemoryTracker::remove(void* p)
	{
		if(!p) return;

		MT::SpinLock lock(m_spin_mutex);

		EntryTable::iterator it = m_map.find(p);
		ASSERT(it != m_map.end() && "Allocated/Dealocataed from different places?");
		if(it != m_map.end())
		{
			m_allocated_memory -= (*it).size();
			m_map.erase(it);
		}
	}

	void MemoryTracker::dumpDetailed()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		MT::SpinLock lock(m_spin_mutex);
		int32_t count = m_map.size();

		if (count)
		{
			memTrackerLog("MemoryTracker", "MemoryTracker Detected memory leaks!");
			memTrackerLog("MemoryTracker", "Dumping objects ->");
		}
		else
		{
			memTrackerLog("MemoryTracker", "MemoryTracker No leaks detected!");
		}

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			char string[512];

			Entry& entry = *it;
			void* adr = it.key();

			if (entry.file() != NULL)
			{
				sprintf(string, "%s(%d): {%d} normal block at %p, %d bytes long.", entry.file(), entry.line(), entry.allocID(), adr, entry.size());
			}
			else
			{
				sprintf(string, "{%d} normal block at %p, %d bytes long.", entry.allocID(), adr, entry.size());
			}
			memTrackerLog("MemoryTracker", "%s", string);

			int32_t str_len = Math::min(16, (int32_t)entry.size());
			char asci_buf[17];
			memset(asci_buf, 0, 17);
			memcpy(asci_buf, adr, str_len);

			sprintf(string, "Data: <%s>", asci_buf);
			for (int j = 0; j < str_len; j++)
			{
				char hex[4];
				memset (hex, 0, sizeof(hex));
				sprintf(hex, " %.2X", *((uint8_t*)adr + j));
				strcat(string, hex);
			}
			memTrackerLog("MemoryTracker", "%s", string);
		}
		if(count)
		{
			memTrackerLog("MemoryTracker", "	  Object dump complete.");
		}
	}

	void MemoryTracker::dumpSortedByAllocationOrder()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		MT::SpinLock lock(m_spin_mutex);
		int count = m_map.size();

		if (count)
		{
			memTrackerLog("MemoryTracker", "MemoryTracker Detected memory leaks!");
			memTrackerLog("MemoryTracker", "Dumping objects ->");
		}
		else
		{
			memTrackerLog("MemoryTracker", "MemoryTracker No leaks detected!");
		}

		map_alloc_order alloc_order_map;
		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			Entry& entry = *it;
			alloc_order_map.insert(entry.allocID(), &entry);
		}

		for (map_alloc_order::iterator it = alloc_order_map.begin(); it != alloc_order_map.end(); ++it)
		{
			char string[512];
			Entry& entry = *(it.second());
			if (entry.file() != NULL)
			{
				sprintf(string, "%s(%d): {%d} normal block, %d bytes long.", entry.file(), entry.line(), entry.allocID(), entry.size());
			}
			else
			{
				sprintf(string, "{%d} normal block, %d bytes long.", entry.allocID(), entry.size());
			}

			memTrackerLog("MemoryTracker", "%s", string);
		}

		if(count)
		{
			memTrackerLog("MemoryTracker", "	  Object dump complete.");
		}
	}

	void MemoryTracker::dumpTruncatedPerFileLine()
	{
		memTrackerLog("MemoryTracker", "Dumping objects ->");

		file_line_map report_map;
		{
			MT::SpinLock lock(m_spin_mutex);
			for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
			{
				Entry& entry = *it;

				FileLineReport r;
				r.file = entry.file();
				r.line = entry.line();

				file_line_map::iterator rit = report_map.find(r);
				if(rit != report_map.end())
					rit.second() += entry.size();
				else
					report_map.insert(r, entry.size());
			}
		}

		for (file_line_map::iterator it = report_map.begin(); it != report_map.end(); ++it)
		{
			char string[512];

			const FileLineReport &rep = it.first();
			intptr_t size = it.second();

			const char *file = rep.file ? rep.file : "unknown";

			if(size >= 1000000)
				sprintf(string, "%30s(%5d) : %2d %03d %03d", file, rep.line, size / 1000000, (size % 1000000) / 1000, (size & 1000));
			else if(size >= 1000)
				sprintf(string, "%30s(%5d) : %6d %03d", file, rep.line, size / 1000, size % 1000);
			else
				sprintf(string, "%30s(%5d) : %10d", file, rep.line, size);

			memTrackerLog("MemoryTracker", "%s", string);
		}

		memTrackerLog("MemoryTracker", "Object dump complete.");
	}

	void MemoryTracker::dumpTruncatedPerFile()
	{
		memTrackerLog("MemoryTracker", "Dumping objects ->");

		file_map report_map;
		{
			MT::SpinLock lock(m_spin_mutex);
			for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
			{
				Entry& entry = *it;


				file_map::iterator rit = report_map.find(entry.file());
				if(rit != report_map.end())
					rit.second() += entry.size();
				else
					report_map.insert(entry.file(), entry.size());
			}
		}

		for (file_map::iterator it = report_map.begin(); it != report_map.end(); ++it)
		{
			char string[512];

			intptr_t size = it.second();
			const char *file = it.first();

			if(size >= 1000000)
				sprintf(string, "%30s : %2d %03d %03d", file, size / 1000000, (size % 1000000) / 1000, (size & 1000));
			else if(size >= 1000)
				sprintf(string, "%30s : %6d %03d", file, size / 1000, size % 1000);
			else
				sprintf(string, "%30s : %10d", file, size);

			memTrackerLog("MemoryTracker", "%s", string);
		}

		memTrackerLog("MemoryTracker", "Object dump complete.");
	}

	void MemoryTracker::markAll()
	{
		MT::SpinLock lock(m_spin_mutex);

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			(*it).mark();
		}
		++m_mark;
	}

	void MemoryTracker::dumpUnmarked()
	{
		// Detected memory leaks!
		// Dumping objects ->
		// {147} normal block at 0x003AF7C0, 10 bytes long.
		// Data: <          > CD CD CD CD CD CD CD CD CD CD
		// d:\temp\zmazat\tt\tt\mainfrm.cpp(34) : {145} normal block at 0x003AF760, 30 bytes long.
		// Data: <               > CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
		//    Object dump complete.

		//	Lock lock(m_lock);

		MT::SpinLock lock(m_spin_mutex);

		std::size_t size = 0;

		memTrackerLog("MemoryTracker", "Dumping objects ->");

		for (EntryTable::iterator it = m_map.begin(); it != m_map.end(); ++it)
		{
			char string[512];

			Entry& entry = *it;
			void* adr = it.key();

			if (0 == entry.getMark() || m_mark == entry.getMark())
				continue;

			size += entry.size();

			if (entry.file() != NULL)
			{
				sprintf(string, "%s(%d) : {%d} normal block at %p, %d bytes long.", entry.file(), entry.line(), entry.allocID(), adr, entry.size());
			}
			else
			{
				sprintf(string, "{%d} normal block at %p, %d bytes long.", entry.allocID(), adr, entry.size());
			}

			memTrackerLog("MemoryTracker", "%s", string);

			int str_len = Math::min(16, (int)entry.size());
			char asci_buf[17];
			memset(asci_buf, 0, 17);
			memcpy(asci_buf, adr, str_len);

			sprintf(string, "Data: <%s>", asci_buf);
			for (int j = 0; j < str_len; j++)
			{
				char hex[4];
				memset (hex, 0, sizeof(hex));
				sprintf(hex, " %.2X", *((uint8_t*)adr + j));
				strcat(string, hex);
			}

			memTrackerLog("MemoryTracker", "%s", string);
		}

		if (0 < size) {
			memTrackerLog("MemoryTracker", "Size of all objects: %u", size);
		}
	}

	MemoryTracker::MemoryTracker()
		: m_spin_mutex(false)
		, m_mark(0)
		, m_allocated_memory(0)
	{
	}

	MemoryTracker::~MemoryTracker()
	{
	}

	//TODO: PC only
	// Typedef for the function pointer
	typedef void (*_PVFV)(void);

	static void LastOnExitFunc()
	{
		Lux::MemoryTracker::getInstance().dumpDetailed();
		Lux::MemoryTracker::destruct();
	}

	static void CInit()
	{
		atexit(&LastOnExitFunc);
	}

	// Define where our segment names
#define SEGMENT_C_INIT      ".CRT$XIM"

	// Build our various function tables and insert them into the correct segments.
#pragma data_seg(SEGMENT_C_INIT)
#pragma data_seg() // Switch back to the default segment

	// Call create our call function pointer arrays and place them in the segments created above
#define SEG_ALLOCATE(SEGMENT)   __declspec(allocate(SEGMENT))
	SEG_ALLOCATE(SEGMENT_C_INIT) _PVFV c_init_funcs[] = { &CInit };

} //~namespace Lux

#endif //~MEM_TRACK