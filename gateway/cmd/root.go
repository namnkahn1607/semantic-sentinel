package cmd

import "github.com/spf13/cobra"

var rootCmd = &cobra.Command{
	Use:   "strix",
	Short: "Strix - Semantic Cache Proxy CLI",
	Long: `Strix is a high-performance semantic cache gateway.
	Use 'strix init' to set up your environment, then 'strix serve' to start.`,
}

func init() {
	rootCmd.AddCommand(initCmd)
}
