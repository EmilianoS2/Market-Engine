"""Exercise the existing HTTP server and real C++ backend with an isolated CSV."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('orderbook_server', ROOT / 'web' / 'server.py')
server_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server_module)

class MarketMakerHTTPTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='market_maker_http_test_')
        cls.engine = server_module.OrderBookEngine(workdir=cls.directory.name)
        class QuietHandler(server_module.Handler):
            def log_message(self, *_):
                pass
        cls.server = server_module.ThreadingHTTPServer(('127.0.0.1', 0), QuietHandler)
        cls.server.engine = cls.engine
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()
        cls.engine.close(); cls.directory.cleanup()

    def request(self, path, data=None):
        request = Request(self.base + path, data=None if data is None else json.dumps(data).encode(), headers={'Content-Type': 'application/json'})
        try:
            with urlopen(request, timeout=10) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            return error.code, json.load(error)

    def setUp(self):
        self.request('/api/reset', {})
        with (Path(self.directory.name) / 'trades.csv').open() as file:
            self.initial_csv_rows = len(list(csv.DictReader(file)))

    def test_market_maker_lifecycle(self):
        defaults = dict(referencePrice='10000', spreadBps='500', quoteSize='5', refreshIntervalMs='100000',
                        skewBpsPerShare='2', minimumInventory='0', targetInventory='100', maximumInventory='150')
        _, initial = self.request('/api/state')
        self.assertEqual(initial['marketMaker']['state'], 'Stopped')
        _, started = self.request('/api/market-maker', dict(action='start', **defaults))
        maker = started['marketMaker']
        self.assertEqual((maker['bid']['price'], maker['ask']['price']), ('9750', '10250'))
        _, state = self.request('/api/state')
        self.assertEqual(state['candles'], [])
        self.assertEqual(state['asks'][0]['marketMakerQuantity'], '5')
        _, fill = self.request('/api/orders', dict(id='1', side='buy', price='10300', quantity='3'))
        self.assertEqual(fill['trades'][0]['seller'], 'MM_LUFFY')
        _, state = self.request('/api/state')
        self.assertEqual((state['marketMaker']['cash'], state['marketMaker']['inventory']), ('1030750', '97'))
        self.assertEqual(state['marketMaker']['ask']['quantity'], '2')
        self.assertEqual(state['candles'][0]['volume'], '3')
        _, rejected = self.request('/api/cancel', dict(id=maker['bid']['id']))
        self.assertFalse(rejected['ok'])
        status, invalid = self.request('/api/market-maker', dict(action='configure', **{**defaults, 'spreadBps': '0'}))
        self.assertEqual(status, 400); self.assertFalse(invalid['ok'])
        _, stopped = self.request('/api/market-maker', dict(action='stop'))
        self.assertEqual(stopped['marketMaker']['cash'], '1030750')
        self.assertEqual(stopped['marketMaker']['reservedCash'], '0')
        self.assertIsNone(stopped['marketMaker']['bid'])
        _, restarted = self.request('/api/market-maker', dict(action='start', **defaults))
        self.assertEqual(restarted['marketMaker']['inventory'], '97')
        _, simulated = self.request('/api/orders', dict(id='100', side='buy', price='9000', quantity='2', source='traffic'))
        self.assertTrue(simulated['resting'])
        _, cancelled = self.request('/api/cancel', dict(id='100', source='traffic', side='buy'))
        self.assertTrue(cancelled['cancelled'])
        self.request('/api/reset', {})
        _, cleared = self.request('/api/state')
        self.assertEqual(cleared['bids'], []); self.assertEqual(cleared['asks'], [])
        self.assertEqual(cleared['candles'], []); self.assertEqual(cleared['marketMaker']['cash'], '1000000')
        self.assertEqual(cleared['marketMaker']['inventory'], '100')
        with (Path(self.directory.name) / 'trades.csv').open() as file:
            rows = list(csv.DictReader(file))
        self.assertEqual(len(rows), self.initial_csv_rows + 1)
        self.assertEqual(rows[-1]['Quantity'], '3')

    def test_market_orders(self):
        _, initial = self.request('/api/state')
        self.assertTrue(initial['marketOrdersSupported'])
        _, empty = self.request('/api/orders', dict(id='1', side='buy', type='market', quantity='5'))
        self.assertEqual(empty['status'], 'cancelled')
        self.assertEqual((empty['filledQuantity'], empty['cancelledQuantity'], empty['remaining']), ('0', '5', '0'))
        self.assertFalse(empty['resting'])
        defaults = dict(action='start', referencePrice='10000', spreadBps='500', quoteSize='5', refreshIntervalMs='100000',
                        skewBpsPerShare='0', minimumInventory='0', targetInventory='100', maximumInventory='150')
        self.request('/api/market-maker', defaults)
        _, bought = self.request('/api/orders', dict(id='2', side='buy', type='market', quantity='3'))
        self.assertEqual(bought['status'], 'filled')
        self.assertEqual(bought['trades'][0]['price'], '10250')
        self.request('/api/orders', dict(id='3', side='sell', price='10300', quantity='3', source='traffic'))
        _, swept = self.request('/api/orders', dict(id='4', side='buy', type='market', quantity='8'))
        self.assertEqual(swept['status'], 'partially_filled_cancelled')
        self.assertEqual((swept['filledQuantity'], swept['cancelledQuantity']), ('5', '3'))
        self.assertEqual([e['price'] for e in swept['trades']], ['10250', '10300'])
        _, state = self.request('/api/state')
        self.assertEqual(state['asks'], [])
        self.assertEqual((state['marketMaker']['cash'], state['marketMaker']['inventory']), ('1051250', '95'))
        self.assertEqual(state['candles'][0]['volume'], '8')
        _, sold = self.request('/api/orders', dict(id='5', side='sell', type='market', quantity='2'))
        self.assertEqual(sold['trades'][0]['price'], '9750')
        _, final = self.request('/api/state')
        self.assertEqual((final['marketMaker']['cash'], final['marketMaker']['inventory']), ('1031750', '97'))
        self.assertEqual(final['candles'][0]['volume'], '10')
        with (Path(self.directory.name) / 'trades.csv').open() as file:
            new_rows = list(csv.DictReader(file))[self.initial_csv_rows:]
        self.assertEqual([r['Price'] for r in new_rows], ['10250', '10250', '10300', '9750'])
        self.assertEqual(sum(int(r['Quantity']) for r in new_rows), 10)
        for body in [dict(id='6', side='buy', type='market', quantity='2', price='10000'),
                     dict(id='6', side='buy', type='unknown', quantity='2'),
                     dict(id='6', side='buy', type='market', quantity='0'),
                     dict(id='6', side='sell', type='market', quantity='-1')]:
            status, rejected = self.request('/api/orders', body)
            self.assertEqual(status, 400); self.assertFalse(rejected['ok'])
        _, unchanged = self.request('/api/state')
        self.assertEqual(unchanged['candles'], final['candles'])
        self.assertEqual(unchanged['marketMaker']['cash'], final['marketMaker']['cash'])

if __name__ == '__main__':
    unittest.main()
