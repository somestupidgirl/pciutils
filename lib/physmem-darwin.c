/*
 *      The PCI Library -- Physical memory mapping for macOS via DirectHW
 *
 *      Copyright (c) 2026
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * macOS has no /dev/mem, so the generic POSIX physmem backend cannot map
 * physical memory. Instead we use the DirectHW kernel extension, whose
 * map_physical() maps an arbitrary physical range into the calling process
 * (via IOConnectMapMemory with caching inhibited), which is exactly what the
 * ECAM access method needs to reach memory-mapped PCIe configuration space.
 *
 * DirectHW must be installed (kext loaded, header + libDirectHW available) and
 * lspci must run as root.
 */

#include "internal.h"
#include "physmem.h"

#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>	/* MAP_FAILED */

#include <DirectHW.h>

struct physmem
{
  int dummy;
};

void
physmem_init_config(struct pci_access *a UNUSED)
{
  /* Nothing to configure: DirectHW connects to its kext, there is no device path. */
}

int
physmem_access(struct pci_access *a, int w)
{
  a->debug("checking DirectHW access for %s mode...", w ? "read/write" : "read-only");

  /* DirectHW enforces the actual privilege check in the kernel; refuse early
   * for non-root so the caller gets a sensible errno. */
  if (getuid() != 0)
    {
      errno = EACCES;
      return -1;
    }

  return 0;
}

struct physmem *
physmem_open(struct pci_access *a, int w)
{
  struct physmem *physmem;

  a->debug("opening DirectHW for %s mode...", w ? "read/write" : "read-only");

  /* iopl() opens a connection to the DirectHW kext (and registers cleanup at
   * exit). It returns 0 on success, -1 if not root or the kext is not loaded. */
  if (iopl(0) < 0)
    {
      a->debug("DirectHW not available (is DirectHW.kext loaded and are you root?)...");
      return NULL;
    }

  physmem = pci_malloc(a, sizeof(*physmem));
  return physmem;
}

void
physmem_close(struct physmem *physmem)
{
  /* The DirectHW connection is torn down by its atexit() handler; just free our
   * bookkeeping. */
  pci_mfree(physmem);
}

long
physmem_get_pagesize(struct physmem *physmem UNUSED)
{
  return sysconf(_SC_PAGESIZE);
}

void *
physmem_map(struct physmem *physmem UNUSED, u64 addr, size_t length, int w UNUSED)
{
  void *ptr;

  /*
   * DirectHW maps physical memory read-only (kIODirectionIn); write-back to
   * config space is therefore not supported through this backend.
   */
  ptr = map_physical(addr, length);
  if (ptr == MAP_FAILED || ptr == NULL)
    {
      if (!errno)
        errno = ENXIO;
      return (void *)-1;
    }

  return ptr;
}

int
physmem_unmap(struct physmem *physmem UNUSED, void *ptr, size_t length)
{
  /* DirectHW's unmap_physical() is a no-op; the mapping persists until the
   * process exits. Report success so callers do not treat this as an error. */
  unmap_physical(ptr, length);
  return 0;
}
