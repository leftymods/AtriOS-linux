"""
Drop-in FastAPI router for ai.leftymods.tech to offload routine Yandex Music
catalog/collection/search/orbs metadata from the speaker CPU to the server.

Mount in your FastAPI app:
    from server_ymusic_offload import router as ymusic_offload_router
    app.include_router(ymusic_offload_router)

Requires: httpx (or requests). Stores per-device OAuth token (synced from app/speaker
or passed via X-YMusic-Token header / device registry) and caches responses in RAM.
"""
import time
import urllib.parse
from typing import Any, Dict, List, Optional, Tuple

import httpx
from fastapi import APIRouter, Header, HTTPException, Request

router = APIRouter()

YM_API = "https://api.music.yandex.net"
YM_HEADERS = {
    "X-Yandex-Music-Client": "YandexMusicAndroid/24023621",
    "Accept-Language": "ru",
}

# Simple in-memory caches: key -> (ts, payload)
_CACHE: Dict[str, Tuple[float, Dict[str, Any]]] = {}


def _cover_url(uri: Optional[str]) -> str:
    if not uri:
        return ""
    u = uri.replace("%%", "200x200")
    return u if u.startswith("http") else f"https://{u}"


async def _ym_get(token: str, path: str) -> Dict[str, Any]:
    hdrs = {**YM_HEADERS, "Authorization": f"OAuth {token}"}
    async with httpx.AsyncClient(timeout=12.0) as client:
        r = await client.get(f"{YM_API}{path}", headers=hdrs)
        r.raise_for_status()
        return r.json()


async def _ym_post_form(token: str, path: str, data: Dict[str, str]) -> Dict[str, Any]:
    hdrs = {**YM_HEADERS, "Authorization": f"OAuth {token}"}
    async with httpx.AsyncClient(timeout=12.0) as client:
        r = await client.post(f"{YM_API}{path}", headers=hdrs, data=data)
        r.raise_for_status()
        return r.json()


def _parse_track(t: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    tid = str(t.get("id") or "").strip()
    if not tid:
        return None
    artists = ", ".join(a.get("name", "") for a in (t.get("artists") or []) if a.get("name"))
    albums = t.get("albums") or []
    first_al = albums[0] if albums else {}
    return {
        "id": tid,
        "title": t.get("title") or "Unknown Track",
        "artist": artists,
        "album": first_al.get("title") or "",
        "cover_url": _cover_url(t.get("coverUri") or first_al.get("coverUri")),
        "duration_ms": int(t.get("durationMs") or 0),
    }


def _parse_album(a: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    aid = str(a.get("id") or "").strip()
    if not aid:
        return None
    artists = ", ".join(x.get("name", "") for x in (a.get("artists") or []) if x.get("name"))
    return {
        "id": aid,
        "title": a.get("title") or "Unknown Album",
        "artist": artists,
        "year": int(a.get("year") or 0),
        "track_count": int(a.get("trackCount") or 0),
        "cover_url": _cover_url(a.get("coverUri")),
    }


async def _resolve_ym_token(device_id: str, x_ymusic_token: Optional[str]) -> Tuple[str, int]:
    """Resolve the user's Yandex OAuth token + uid (from header or your device store)."""
    token = (x_ymusic_token or "").strip()
    if not token:
        raise HTTPException(status_code=404, detail="No YMusic token for server offload; fallback to speaker")
    acc = await _ym_get(token, "/account/status")
    uid = int(((acc.get("result") or {}).get("account") or {}).get("uid") or 0)
    if uid <= 0:
        raise HTTPException(status_code=401, detail="Invalid YMusic token")
    return token, uid


@router.post("/relay/v1/app/{device_id}/ymusic/{op}")
async def relay_ymusic_offload(
    device_id: str,
    op: str,
    request: Request,
    x_ymusic_token: Optional[str] = Header(default=None, alias="X-YMusic-Token"),
):
    body = await request.json()
    token, uid = await _resolve_ym_token(device_id, x_ymusic_token)
    now = time.time()

    if op == "search":
        q = str(body.get("query") or "").strip()
        if not q:
            return {"reply": {"type": "ymusic_search_response", "success": True, "tracks": [], "albums": []}}
        resp = await _ym_get(token, f"/search?type=all&page=0&nocorrect=false&text={urllib.parse.quote(q)}")
        res = resp.get("result") or {}
        tracks = [t for t in (_parse_track(x) for x in ((res.get("tracks") or {}).get("results") or [])[:12]) if t]
        albums = [a for a in (_parse_album(x) for x in ((res.get("albums") or {}).get("results") or [])[:8]) if a]
        return {"reply": {"type": "ymusic_search_response", "success": True, "query": q, "tracks": tracks, "albums": albums}}

    if op == "collection":
        ck = f"col:{uid}"
        if ck in _CACHE and now - _CACHE[ck][0] < 300:
            return {"reply": _CACHE[ck][1]}

        likes_resp = await _ym_get(token, f"/users/{uid}/likes/tracks")
        raw_likes = (((likes_resp.get("result") or {}).get("library") or {}).get("tracks") or [])
        likes_count = len(raw_likes)
        ids = [str(x.get("id")) for x in raw_likes[:30] if x.get("id")]
        liked_tracks: List[Dict[str, Any]] = []
        if ids:
            det = await _ym_post_form(token, "/tracks", {"track-ids": ",".join(ids)})
            liked_tracks = [t for t in (_parse_track(x) for x in (det.get("result") or [])) if t]

        fav_artists: List[Dict[str, Any]] = []
        seen = set()
        try:
            ar = await _ym_get(token, f"/users/{uid}/likes/artists")
            for item in (ar.get("result") or [])[:10]:
                a = item.get("artist") or item
                name = (a.get("name") or "").strip()
                if name and name.lower() not in seen:
                    seen.add(name.lower())
                    fav_artists.append({
                        "id": str(a.get("id") or name),
                        "name": name,
                        "cover_url": _cover_url((a.get("cover") or {}).get("uri") or a.get("ogImage")),
                        "track_count": 0,
                    })
        except Exception:
            pass

        playlists: List[Dict[str, Any]] = []
        try:
            pr = await _ym_get(token, f"/users/{uid}/playlists/list")
            for pl in (pr.get("result") or [])[:12]:
                title = (pl.get("title") or "").strip()
                if title:
                    cov = pl.get("cover") or {}
                    uri = cov.get("uri") or ((cov.get("itemsUri") or [None])[0]) or pl.get("ogImage")
                    playlists.append({
                        "kind": int(pl.get("kind") or 0),
                        "title": title,
                        "track_count": int(pl.get("trackCount") or 0),
                        "cover_url": _cover_url(uri),
                    })
        except Exception:
            pass

        liked_albums: List[Dict[str, Any]] = []
        try:
            alr = await _ym_get(token, f"/users/{uid}/likes/albums")
            aids = [str(x.get("id") or (x.get("album") or {}).get("id") or "") for x in (alr.get("result") or [])[:12]]
            aids = [x for x in aids if x]
            if aids:
                adet = await _ym_post_form(token, "/albums", {"album-ids": ",".join(aids)})
                liked_albums = [a for a in (_parse_album(x) for x in (adet.get("result") or [])) if a]
        except Exception:
            pass

        dislikes_count = 0
        try:
            dr = await _ym_get(token, f"/users/{uid}/dislikes/tracks")
            dislikes_count = len((((dr.get("result") or {}).get("library") or {}).get("tracks") or []))
        except Exception:
            pass

        reply = {
            "type": "ymusic_collection_response",
            "success": True,
            "collection": {
                "likes_count": likes_count,
                "dislikes_count": dislikes_count,
                "liked_tracks": liked_tracks,
                "favorite_artists": fav_artists,
                "playlists": playlists,
                "liked_albums": liked_albums,
            },
        }
        _CACHE[ck] = (now, reply)
        return {"reply": reply}

    raise HTTPException(status_code=404, detail=f"Unsupported offload op: {op}")
