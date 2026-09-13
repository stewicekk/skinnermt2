# model.py - simple PyTorch model for per-vertex weight prediction
import torch.nn as nn
class SimpleWeightNet(nn.Module):
    def __init__(self, in_ch=3, hidden=128, out_ch=32):
        super(SimpleWeightNet, self).__init__()
        self.net = nn.Sequential(
            nn.Linear(in_ch, hidden),
            nn.ReLU(),
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, out_ch)
        )
    def forward(self, x):
        return self.net(x)
