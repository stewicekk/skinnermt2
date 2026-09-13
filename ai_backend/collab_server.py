#!/usr/bin/env python3
"""
WebSocket Real-time Collaboration Server for Metin2 Rigging Studio
Provides real-time multi-user editing, presence, and synchronization
"""

import asyncio
import json
import uuid
from contextlib import asynccontextmanager
from datetime import datetime
from typing import Dict, Optional, Any
from dataclasses import dataclass, field, asdict
from enum import Enum

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import uvicorn


class MessageType(str, Enum):
    # Connection
    CONNECT = "connect"
    DISCONNECT = "disconnect"
    HEARTBEAT = "heartbeat"
    
    # Presence
    USER_JOINED = "user_joined"
    USER_LEFT = "user_left"
    USER_LIST = "user_list"
    CURSOR_UPDATE = "cursor_update"
    
    # Editing
    WEIGHT_PAINT = "weight_paint"
    BONE_SELECT = "bone_select"
    BRUSH_CHANGE = "brush_change"
    SYMMETRY_TOGGLE = "symmetry_toggle"
    MESH_VISIBILITY = "mesh_visibility"
    
    # History
    UNDO = "undo"
    REDO = "redo"
    HISTORY_SYNC = "history_sync"
    
    # File Operations
    MODEL_LOADED = "model_loaded"
    EXPORT_STARTED = "export_started"
    EXPORT_COMPLETED = "export_completed"
    
    # AI Operations
    AI_TRANSFER_STARTED = "ai_transfer_started"
    AI_TRANSFER_COMPLETED = "ai_transfer_completed"
    
    # Chat/Communication
    CHAT_MESSAGE = "chat_message"
    ANNOTATION_ADD = "annotation_add"
    ANNOTATION_REMOVE = "annotation_remove"
    
    # System
    ERROR = "error"
    NOTIFICATION = "notification"


@dataclass
class User:
    id: str
    name: str
    color: str
    avatar_url: Optional[str] = None
    cursor_position: Optional[Dict[str, float]] = None
    selected_bone: Optional[str] = None
    brush_settings: Optional[Dict[str, Any]] = None
    joined_at: datetime = field(default_factory=datetime.now)
    last_activity: datetime = field(default_factory=datetime.now)
    is_active: bool = True
    metadata: Dict[str, Any] = field(default_factory=dict)
    websocket: Optional[Any] = None


def public_user(user: User) -> Dict[str, Any]:
    data = asdict(user)
    data.pop("websocket", None)
    data["joined_at"] = user.joined_at.isoformat()
    data["last_activity"] = user.last_activity.isoformat()
    return data


@dataclass
class Room:
    id: str
    name: str
    owner_id: str
    users: Dict[str, User] = field(default_factory=dict)
    model_data: Optional[Dict[str, Any]] = None
    history: list = field(default_factory=list)
    history_index: int = -1
    created_at: datetime = field(default_factory=datetime.now)
    updated_at: datetime = field(default_factory=datetime.now)
    is_public: bool = False
    max_users: int = 10
    settings: Dict[str, Any] = field(default_factory=dict)
    
    def add_user(self, user: User) -> bool:
        if len(self.users) >= self.max_users:
            return False
        self.users[user.id] = user
        self.updated_at = datetime.now()
        return True
    
    def remove_user(self, user_id: str) -> bool:
        if user_id in self.users:
            del self.users[user_id]
            self.updated_at = datetime.now()
            return True
        return False
    
    def get_user(self, user_id: str) -> Optional[User]:
        return self.users.get(user_id)


class CollaborationManager:
    def __init__(self):
        self.rooms: Dict[str, Room] = {}
        self.user_rooms: Dict[str, str] = {}  # user_id -> room_id
        self.connections: Dict[str, WebSocket] = {}  # user_id -> websocket
    
    async def _send(self, user: User, message: Dict[str, Any]):
        websocket = self.connections.get(user.id) or user.websocket
        if not websocket:
            return
        try:
            await websocket.send_text(json.dumps(message))
        except Exception:
            return
    
    async def broadcast(self, room: Room, message: Dict[str, Any], exclude: Optional[str] = None):
        await asyncio.gather(*[
            self._send(user, message)
            for user_id, user in room.users.items()
            if user_id != exclude and user.is_active
        ])
    
    def create_room(self, name: str, owner_id: str, is_public: bool = False, max_users: int = 10) -> Room:
        room_id = str(uuid.uuid4())[:8]
        room = Room(
            id=room_id,
            name=name,
            owner_id=owner_id,
            is_public=is_public,
            max_users=max_users
        )
        self.rooms[room_id] = room
        return room
    
    def get_room(self, room_id: str) -> Optional[Room]:
        return self.rooms.get(room_id)
    
    def join_room(self, room_id: str, user: User, websocket: Optional[WebSocket] = None) -> bool:
        room = self.get_room(room_id)
        if not room:
            return False
        
        if room.add_user(user):
            self.user_rooms[user.id] = room_id
            if websocket is not None:
                self.connections[user.id] = websocket
                user.websocket = websocket
            user.last_activity = datetime.now()
            return True
        return False
    
    async def leave_room(self, user_id: str):
        room_id = self.user_rooms.get(user_id)
        if room_id:
            room = self.get_room(room_id)
            if room:
                room.remove_user(user_id)
                # Notify others
                await self.broadcast(room, {
                    "type": MessageType.USER_LEFT,
                    "payload": {"user_id": user_id}
                }, exclude=user_id)
            
            del self.user_rooms[user_id]
        
        if user_id in self.connections:
            del self.connections[user_id]
    
    async def handle_message(self, user_id: str, message: Dict[str, Any]):
        room_id = self.user_rooms.get(user_id)
        if not room_id:
            return
        
        room = self.get_room(room_id)
        if not room:
            return
        
        user = room.get_user(user_id)
        if not user:
            return
        
        user.last_activity = datetime.now()
        msg_type = message.get("type")
        payload = message.get("payload", {})
        
        # Handle different message types
        if msg_type == MessageType.CURSOR_UPDATE:
            user.cursor_position = payload
            await self.broadcast(room, {
                "type": MessageType.CURSOR_UPDATE,
                "payload": {"user_id": user_id, "position": payload, "user_name": user.name, "user_color": user.color}
            }, exclude=user_id)
        
        elif msg_type == MessageType.WEIGHT_PAINT:
            # Broadcast weight paint operation
            await self.broadcast(room, {
                "type": MessageType.WEIGHT_PAINT,
                "payload": {**payload, "user_id": user_id}
            }, exclude=user_id)
            
            # Add to history
            room.history = room.history[:room.history_index + 1]
            room.history.append({
                "type": "weight_paint",
                "payload": payload,
                "user_id": user_id,
                "timestamp": datetime.now().isoformat()
            })
            room.history_index = len(room.history) - 1
        
        elif msg_type == MessageType.BONE_SELECT:
            user.selected_bone = payload.get("bone_id")
            await self.broadcast(room, {
                "type": MessageType.BONE_SELECT,
                "payload": {"user_id": user_id, "bone_id": payload.get("bone_id"), "user_name": user.name}
            }, exclude=user_id)
        
        elif msg_type == MessageType.BRUSH_CHANGE:
            user.brush_settings = payload
            await self.broadcast(room, {
                "type": MessageType.BRUSH_CHANGE,
                "payload": {**payload, "user_id": user_id}
            }, exclude=user_id)
        
        elif msg_type == MessageType.SYMMETRY_TOGGLE:
            await self.broadcast(room, {
                "type": MessageType.SYMMETRY_TOGGLE,
                "payload": {**payload, "user_id": user_id}
            }, exclude=user_id)
        
        elif msg_type == MessageType.MESH_VISIBILITY:
            await self.broadcast(room, {
                "type": MessageType.MESH_VISIBILITY,
                "payload": {**payload, "user_id": user_id}
            }, exclude=user_id)
        
        elif msg_type == MessageType.UNDO:
            if room.history_index > 0:
                room.history_index -= 1
                action = room.history[room.history_index]
                await self.broadcast(room, {
                    "type": MessageType.UNDO,
                    "payload": {"action": action, "history_index": room.history_index}
                })
        
        elif msg_type == MessageType.REDO:
            if room.history_index < len(room.history) - 1:
                room.history_index += 1
                action = room.history[room.history_index]
                await self.broadcast(room, {
                    "type": MessageType.REDO,
                    "payload": {"action": action, "history_index": room.history_index}
                })
        
        elif msg_type == MessageType.CHAT_MESSAGE:
            await self.broadcast(room, {
                "type": MessageType.CHAT_MESSAGE,
                "payload": {
                    "user_id": user_id,
                    "user_name": user.name,
                    "user_color": user.color,
                    "message": payload.get("message", ""),
                    "timestamp": datetime.now().isoformat()
                }
            })
        
        elif msg_type == MessageType.ANNOTATION_ADD:
            await self.broadcast(room, {
                "type": MessageType.ANNOTATION_ADD,
                "payload": {**payload, "user_id": user_id, "user_name": user.name, "user_color": user.color}
            })
        
        elif msg_type == MessageType.ANNOTATION_REMOVE:
            await self.broadcast(room, {
                "type": MessageType.ANNOTATION_REMOVE,
                "payload": {**payload, "user_id": user_id}
            })
        
        elif msg_type == MessageType.HEARTBEAT:
            user.last_activity = datetime.now()
            await self._send(user, {"type": MessageType.HEARTBEAT, "payload": {"timestamp": datetime.now().isoformat()}})


# Global collaboration manager
collab_manager = CollaborationManager()


# FastAPI App
app = FastAPI(title="Metin2 Rigging Studio - Collaboration Server")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class CreateRoomRequest(BaseModel):
    name: str
    owner_name: str
    user_color: Optional[str] = None
    is_public: bool = False
    max_users: int = 10


class JoinRoomRequest(BaseModel):
    room_id: str
    user_name: str
    user_color: Optional[str] = None


@app.get("/health")
async def health():
    return {"status": "ok", "rooms": len(collab_manager.rooms), "users": len(collab_manager.user_rooms)}


@app.post("/api/rooms")
async def create_room(request: CreateRoomRequest):
    owner_id = str(uuid.uuid4())
    owner_color = request.user_color or f"#{hash(request.owner_name) % 0xFFFFFF:06x}"
    
    owner = User(
        id=owner_id,
        name=request.owner_name,
        color=owner_color
    )
    
    room = collab_manager.create_room(
        name=request.name,
        owner_id=owner_id,
        is_public=request.is_public,
        max_users=request.max_users
    )
    
    return {"room": {"id": room.id, "name": room.name, "owner_id": room.owner_id}, "user": public_user(owner)}


@app.post("/api/rooms/{room_id}/join")
async def join_room_endpoint(room_id: str, request: JoinRoomRequest):
    room = collab_manager.get_room(room_id)
    if not room:
        raise HTTPException(status_code=404, detail="Room not found")
    user_id = str(uuid.uuid4())
    user = User(
        id=user_id,
        name=request.user_name,
        color=request.user_color or f"#{abs(hash(request.user_name)) % 0xFFFFFF:06x}"
    )
    if not collab_manager.join_room(room_id, user):
        raise HTTPException(status_code=409, detail="Room is full")
    return {"room_id": room_id, "user": public_user(user)}


@app.get("/api/rooms")
async def list_rooms():
    return {
        "rooms": [
            {
                "id": room.id,
                "name": room.name,
                "owner_id": room.owner_id,
                "user_count": len(room.users),
                "max_users": room.max_users,
                "is_public": room.is_public,
                "created_at": room.created_at.isoformat()
            }
            for room in collab_manager.rooms.values()
        ]
    }


@app.get("/api/rooms/{room_id}")
async def get_room(room_id: str):
    room = collab_manager.get_room(room_id)
    if not room:
        raise HTTPException(status_code=404, detail="Room not found")
    
    return {
        "id": room.id,
        "name": room.name,
        "owner_id": room.owner_id,
        "users": {uid: public_user(u) for uid, u in room.users.items()},
        "user_count": len(room.users),
        "max_users": room.max_users,
        "is_public": room.is_public,
        "created_at": room.created_at.isoformat(),
        "updated_at": room.updated_at.isoformat()
    }


@app.websocket("/ws/{room_id}/{user_id}")
async def websocket_endpoint(websocket: WebSocket, room_id: str, user_id: str):
    await websocket.accept()
    
    room = collab_manager.get_room(room_id)
    if not room:
        await websocket.close(code=4004, reason="Room not found")
        return
    
    user = room.get_user(user_id)
    if not user:
        await websocket.close(code=4003, reason="User not in room")
        return
    
    user.websocket = websocket
    collab_manager.connections[user_id] = websocket
    user.last_activity = datetime.now()
    
    await collab_manager.broadcast(room, {
        "type": MessageType.USER_JOINED,
        "payload": {
            "user_id": user_id,
            "name": user.name,
            "color": user.color,
            "cursor_position": user.cursor_position
        }
    }, exclude=user_id)
    
    # Send current state to joining user
    await websocket.send_text(json.dumps({
        "type": MessageType.USER_LIST,
        "payload": {
            "users": [
                {
                    "id": uid,
                    "name": u.name,
                    "color": u.color,
                    "cursor_position": u.cursor_position,
                    "selected_bone": u.selected_bone
                }
                for uid, u in room.users.items()
            ],
            "model_data": room.model_data,
            "history_index": room.history_index,
            "history_length": len(room.history)
        }
    }))
    
    try:
        while True:
            data = await websocket.receive_text()
            message = json.loads(data)
            await collab_manager.handle_message(user_id, message)
    except WebSocketDisconnect:
        await collab_manager.leave_room(user_id)
    except Exception as e:
        print(f"WebSocket error: {e}")
        await collab_manager.leave_room(user_id)


# Cleanup inactive users periodically
async def cleanup_task():
    while True:
        await asyncio.sleep(60)  # Every minute
        now = datetime.now()
        for room in list(collab_manager.rooms.values()):
            for user_id, user in list(room.users.items()):
                if (now - user.last_activity).seconds > 300:  # 5 min timeout
                    await collab_manager.leave_room(user_id)
                    print(f"Removed inactive user {user_id} from room {room.id}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    asyncio.create_task(cleanup_task())
    print("Collaboration server started")
    yield


app.router.lifespan_context = lifespan


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8001)