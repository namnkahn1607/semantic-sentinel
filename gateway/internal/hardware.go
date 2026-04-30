package internal

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"

	"golang.org/x/sys/unix"
)

const (
	minRAMBytes   = 8 * 1024 * 1024 * 1024
	maxVirtualCPU = 4
)

func Enforce() error {
	if ramErr := checkRAM(); ramErr != nil {
		return ramErr
	}

	return pinProcToCPU(applyCPULimit())
}

func applyCPULimit() (procCPUs int) {
	sysCPUs := runtime.NumCPU()

	if sysCPUs == 1 {
		procCPUs = 1
	} else {
		procCPUs = min(maxVirtualCPU, sysCPUs/2)
	}

	runtime.GOMAXPROCS(procCPUs)
	return
}

func pinProcToCPU(procCPUs int) error {
	var cpuSet unix.CPUSet
	cpuSet.Zero()

	for i := range procCPUs {
		cpuSet.Set(i)
	}

	return unix.SchedSetaffinity(os.Getpid(), &cpuSet)
}

func checkRAM() error {
	memData, readErr := os.ReadFile("/proc/meminfo")
	if readErr != nil {
		return nil
	}

	for _, line := range strings.Split(string(memData), "\n") {
		if !strings.HasPrefix(line, "MemTotal:") {
			continue
		}

		fields := strings.Fields(line)
		if len(fields) < 2 {
			break
		}

		kb, parseErr := strconv.ParseInt(fields[1], 10, 64)
		if parseErr != nil {
			break
		}

		if totalBytes := kb * 1024; totalBytes < minRAMBytes {
			return fmt.Errorf(
				"insufficient RAM. Require at least %dMB", minRAMBytes/(1024*1024),
			)
		}

		return nil
	}

	return nil
}
