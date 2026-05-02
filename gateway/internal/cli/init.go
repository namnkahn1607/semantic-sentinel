package cli

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/cobra"
)

const (
	envDir        = ".strix"
	envFileName   = ".env"
	envPermission = 0600 // rw
	mkPermission  = 0700 // rwx
)

var initCmd = &cobra.Command{
	Use:   "init",
	Short: "Initialize the Strix environment (~/.strix/)",
	Long: `Creates the ~/.strix/ directory and an empty ~/.strix/.env config file.

	The .env file is created with permission 0600 (owner read/write only)
	to prevent API key leakage. Strix will refuse to run if this permission
	is ever widened.`,
	RunE: runInit,
}

func runInit(_ *cobra.Command, _ []string) error {
	home, dirErr := os.UserHomeDir()
	if dirErr != nil {
		return fmt.Errorf("cannot determine home directory: %w", dirErr)
	}

	dir := filepath.Join(home, envDir)
	env := filepath.Join(dir, envFileName)

	// 1. Create ~/.strix/ directory with the highest permission.
	if mkErr := os.MkdirAll(dir, mkPermission); mkErr != nil {
		return fmt.Errorf("cannot create %s: %w", dir, mkErr)
	}

	// 2. Create the environment file ~/.strix/.env.
	file, createErr := os.OpenFile(env, os.O_CREATE|os.O_EXCL|os.O_WRONLY, envPermission)
	switch {
	case createErr == nil && file != nil:
		fmt.Printf("[strix init] Created %s\n", env)

		if closeErr := file.Close(); closeErr != nil {
			fmt.Printf("[strix init] Error closing env-file: %v.\n", closeErr)
		}

	case errors.Is(createErr, os.ErrExist):
		fmt.Printf("[strix init] %s already exists.\n", env)

	default:
		return fmt.Errorf("cannot create %s: %w", env, createErr)
	}

	// 3. Enforce 0600 regardless manual chmod
	if chmodErr := os.Chmod(env, envPermission); chmodErr != nil {
		return fmt.Errorf("SECURITY: Cannot set 0600 on %s due to: %w", env, chmodErr)
	}

	fmt.Println(
		"[strix init] Environment ready. Run 'strix config set' to add your credentials.",
	)
	return nil
}

// EnvFilePath returns the canonical path to ~/.strix/.env.
// Used by other commands to locate the configuration file.
func EnvFilePath() (string, error) {
	home, dirErr := os.UserHomeDir()
	if dirErr != nil {
		return "", fmt.Errorf("cannot determine home directory: %w", dirErr)
	}

	return filepath.Join(home, envDir, envFileName), nil
}

// AssertEnvPermissions returns an error if ~/.strix/.env has permissions
// wider than 0600. Called by 'strix serve' as a boot-time security check.
func AssertEnvPermissions() error {
	envPath, dirErr := EnvFilePath()
	if dirErr != nil {
		return dirErr
	}

	info, statErr := os.Stat(envPath)
	if statErr != nil {
		return fmt.Errorf("cannot stat %s (run 'strix init' first): %w", envPath, statErr)
	}

	if perm := info.Mode().Perm(); perm&0077 != 0 {
		return fmt.Errorf(
			"SECURITY: %s has permissions %04o - group/other bits must be zero. Run 'chmod 0600 %s' to fix",
			envPath, perm, envPath,
		)
	}

	return nil
}
