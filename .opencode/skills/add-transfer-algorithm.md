# Add New Weight Transfer Algorithm

1. Open `C:\rigapp\RigApp\Core\Metin2Core.cs`
2. Find the `WeightTransfer` static class
3. Add a new static method following the pattern:
   ```csharp
   public static void TransferWeightsNewAlgo(MeshData source, MeshData target, float param = 1.0f)
   {
       // Implementation here
   }
   ```
4. Register in `WeightTransferAlgorithm` enum in `BatchConfiguration.cs`
5. Add call in `BatchProcessor.cs` switch statement (line ~217-230)
6. Add option in `BatchConfigurationDialog.xaml` ComboBox (line ~108-113)
7. Build and test
