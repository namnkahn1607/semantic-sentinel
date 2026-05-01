package internal

import (
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"
)

const (
	minRAMBytes   = 8 * 1024 * 1024 * 1024
	maxVirtualCPU = 4
)

func Enforce() error {
	if ramErr := checkRAM(); ramErr != nil {
		return ramErr
	}

	applyCPULimit()
	return nil
}

func applyCPULimit() {
	sysCPUs := runtime.NumCPU()

	var procCPUs int
	if sysCPUs == 1 {
		procCPUs = 1
	} else {
		procCPUs = min(maxVirtualCPU, sysCPUs/2)
	}

	runtime.GOMAXPROCS(procCPUs)
}

func checkRAM() error {
	memData, readErr := os.ReadFile("/proc/meminfo")
	if readErr != nil {
		return fmt.Errorf("cannot read /proc/meminfo: %w", readErr)
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
			return fmt.Errorf("cannot parse RAM size: %w", parseErr)
		}

		if totalBytes := kb * 1024; totalBytes < minRAMBytes {
			return fmt.Errorf(
				"insufficient RAM. Require at least %dMB", minRAMBytes/(1024*1024),
			)
		}

		return nil
	}

	return fmt.Errorf("MemTotal not found in /proc/meminfo")
}
