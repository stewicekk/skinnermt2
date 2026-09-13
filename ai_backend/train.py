# train.py - demo training script
import torch
import numpy as np
from model import SimpleWeightNet

def train_demo():
    X = np.random.randn(2000,3).astype('float32')
    Y = np.random.dirichlet([1.0]*8, size=2000).astype('float32')
    model = SimpleWeightNet(in_ch=3, hidden=64, out_ch=8)
    opt = torch.optim.Adam(model.parameters(), lr=1e-3)
    loss_fn = torch.nn.KLDivLoss(reduction='batchmean')
    for epoch in range(5):
        idx = np.random.permutation(len(X))
        for i in range(0, len(X), 128):
            xb = torch.from_numpy(X[idx[i:i+128]]).float()
            yb = torch.from_numpy(Y[idx[i:i+128]]).float()
            logits = model(xb)
            logp = torch.log_softmax(logits, dim=1)
            loss = loss_fn(logp, yb)
            opt.zero_grad(); loss.backward(); opt.step()
        print('epoch', epoch, 'loss', loss.item())
    torch.save(model.state_dict(), 'demo_model_v2.pth')

if __name__ == '__main__':
    train_demo()
