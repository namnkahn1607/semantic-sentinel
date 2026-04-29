package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

var (
	flagEndpoint string
	flagAPIKey   string
)

var configCmd = &cobra.Command{
	Use:   "config",
	Short: "Manage Strix configuration",
}

var configSetCmd = &cobra.Command{
	Use:   "set",
	Short: "Set credentials in ~/.strix/.env",
	Long: `Writes (or overwrites) the LLM_ENDPOINT and LLM_API_KEY values
	into ~/.strix/.env. The file must exist (run 'strix init' first) and
	must have permission 0600.
 
	The API key is NEVER passed via command-line arguments that would be
	visible in process listings — it is written directly to the secured
	.env file only.`,
	RunE: runConfigSet,
}

func init() {
	configCmd.Flags().StringVar(&flagEndpoint, "endpoint", "", "base URL (required)")
	configCmd.Flags().StringVar(&flagAPIKey, "apikey", "", "API key (required)")
	_ = configCmd.MarkFlagRequired("endpoint")
	_ = configCmd.MarkFlagRequired("apikey")

	configCmd.AddCommand(configSetCmd)
}

func runConfigSet(_ *cobra.Command, _ []string) error {
	if permErr := AssertEnvPermissions(); permErr != nil {
		return permErr
	}

	envPath, pathErr := EnvFilePath()
	if pathErr != nil {
		return pathErr
	}

	content := fmt.Sprintf("ENDPOINT=%s\nAPI_KEY=%s\n", flagEndpoint, flagAPIKey)
	if writeErr := os.WriteFile(envPath, []byte(content), envPermission); writeErr != nil {
		return fmt.Errorf("cannot write to %s: %w", envPath, writeErr)
	}

	if chmodErr := os.Chmod(envPath, envPermission); chmodErr != nil {
		return fmt.Errorf("SECURITY: Cannot enforce 0600 after write: %w", chmodErr)
	}

	fmt.Printf("Credentials saved to %s\n", envPath)
	return nil
}
