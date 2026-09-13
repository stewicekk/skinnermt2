# Add New Export Format

1. Open `C:\rigapp\RigApp\BatchProcessor.cs`
2. Find the `ExportFormat` enum
3. Add new format value
4. Add `ExportNewFormat` static method (following the pattern of `ExportSmd` or `ExportObj`)
5. Update `GetOutputFileName` method to handle new extension
6. Update the switch in `ProcessFile` method
7. Add option in `BatchConfigurationDialog.xaml`
8. Build and test
