package system

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

// ApplyGoLimits sets GOMAXPROCS based on system's total CPUs
// and return it as integer.
func ApplyGoLimits() (goCores int) {
	totalCores := runtime.NumCPU()
	if totalCores < 2 {
		runtime.GOMAXPROCS(1)
		return 1
	}

	goCores = min(maxVirtualCPU, totalCores/2)
	runtime.GOMAXPROCS(goCores)
	return goCores
}

// GenCppLimits generates a taskset -c flag argument used for
// Vector Engine based on number of allocated CPUs for HTTP Gateway.
func GenCppLimits(goCores int) string {
	totalCores := runtime.NumCPU()

	cppCores := make([]string, 0, totalCores-goCores)
	for i := goCores; i < totalCores; i++ {
		cppCores = append(cppCores, strconv.Itoa(i))
	}

	return strings.Join(cppCores, ",")
}

// CheckRAM checks if the system has sufficient amount of RAM (>= 8GB).
func CheckRAM() error {
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
