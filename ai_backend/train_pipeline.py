import os
import json
import math
import torch
import numpy as np
from torch.utils.data import Dataset, DataLoader
from model import SimpleWeightNet
from pathlib import Path

class VertexDataset(Dataset):
    """Expect dataset directory with JSON files where each file contains:
    { "vertices": [[x,y,z],...], "weights": [[w1,w2,...], ...] }
    All rows must match lengths.
    """
    def __init__(self, folder, max_bones=32):
        self.files = list(Path(folder).glob('*.json'))
        self.max_bones = max_bones
        self.samples = []
        for f in self.files:
            j = json.loads(f.read_text())
            verts = np.array(j['vertices'], dtype=np.float32)
            weights = np.array(j['weights'], dtype=np.float32)
            # pad/truncate weights to max_bones
            nb = weights.shape[1]
            if nb < self.max_bones:
                pad = np.zeros((weights.shape[0], self.max_bones - nb), dtype=np.float32)
                weights = np.hstack([weights, pad])
            elif nb > self.max_bones:
                weights = weights[:, :self.max_bones]
            self.samples.append((verts, weights))
        # flatten all verts into dataset entries with per-vertex target
        self.items = []
        for verts, weights in self.samples:
            for i in range(len(verts)):
                self.items.append((verts[i], weights[i]))

    def __len__(self):
        return len(self.items)

    def __getitem__(self, idx):
        v, w = self.items[idx]
        return v, w

def train(folder, epochs=5, batch_size=256, lr=1e-3, out='model_checkpoint.pth', max_bones=32):
    ds = VertexDataset(folder, max_bones=max_bones)
    dl = DataLoader(ds, batch_size=batch_size, shuffle=True, num_workers=0)
    model = SimpleWeightNet(in_ch=3, hidden=128, out_ch=max_bones)
    opt = torch.optim.Adam(model.parameters(), lr=lr)
    loss_fn = torch.nn.KLDivLoss(reduction='batchmean')
    model.train()
    for epoch in range(epochs):
        running = 0.0
        for xb, yb in dl:
            xb = xb.float()
            yb = yb.float()
            logits = model(xb)
            logp = torch.log_softmax(logits, dim=1)
            loss = loss_fn(logp, yb)
            opt.zero_grad(); loss.backward(); opt.step()
            running += loss.item()
        print(f"Epoch {epoch+1}/{epochs} avg_loss: {running/len(dl):.6f}")
        torch.save(model.state_dict(), f"{out}.epoch{epoch+1}.pth")
    torch.save(model.state_dict(), out)
    print('Training finished. Saved:', out)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', required=True, help='folder with JSON snapshots')
    parser.add_argument('--epochs', type=int, default=5)
    parser.add_argument('--batch', type=int, default=256)
    parser.add_argument('--out', default='model_checkpoint.pth')
    parser.add_argument('--max_bones', type=int, default=32)
    args = parser.parse_args()
    train(args.data, epochs=args.epochs, batch_size=args.batch, out=args.out, max_bones=args.max_bones)
